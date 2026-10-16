/*
 File: FileBrowser.cc

 The Newton browsing the desktop's folders (FileBrowser.h).
 */

#include "dock/FileBrowser.h"
#include "dock/DockController.h"
#include "dock/Packages.h"
#include "base/Dates.h"
#include "base/Log.h"
#include "base/NewtonStrings.h"
#include "comms/DockRefs.h"
#include "platform/AppFolders.h"

#include "RSSymbols.h"

#include <algorithm>
#include <chrono>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace ncx {

namespace {

Ref Element(const std::string & inName, int inType)
{
  RefVar element(AllocateFrame());
  SetFrameSlot(element, SYMA(name), MakeStringFromUTF8(inName));
  SetFrameSlot(element, SYMA(type), MAKEINT(inType));
  return element;
}

std::string Lower(std::string inText)
{
  std::transform(inText.begin(), inText.end(), inText.begin(), [](char c) { return (char)std::tolower((unsigned char)c); });
  return inText;
}

/** A path element's name: a string, or a frame with one. */
std::string ElementName(RefArg inElement)
{
  RefVar name(IsFrame(inElement) ? GetFrameSlot(inElement, SYMA(name)) : (Ref)inElement);
  return IsString(name) ? UTF8FromString(name) : std::string();
}

/** A file's time as seconds since 1970. */
int64_t Seconds(const fs::file_time_type & inTime)
{
  auto system = std::chrono::system_clock::now() + (inTime - fs::file_time_type::clock::now());
  return std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
}

} // namespace


FileBrowser::FileBrowser(DockController & ioDock)
  : mFolder(HomeFolder())
{
  ioDock.handle(kDRequestToBrowse, [this](DockController & dock, const DockEvent & e) { requestToBrowse(dock, e); });
  ioDock.handle(kDGetDefaultPath, [this](DockController & dock, const DockEvent &) {
    dock.session().sendRef(kDPath, RefVar(path()));
  });
  ioDock.handle(kDSetPath, [this](DockController & dock, const DockEvent & e) { setPath(dock, e); });
  ioDock.handle(kDGetFilesAndFolders, [this](DockController & dock, const DockEvent &) {
    dock.session().sendRef(kDFilesAndFolders, RefVar(fileList()));
  });
  ioDock.handle(kDGetFileInfo, [this](DockController & dock, const DockEvent & e) { fileInfo(dock, e); });
  ioDock.handle(kDResolveAlias, [](DockController &, const DockEvent &) {
    // there are no aliases among the files we show (as the Cocoa app)
    Log(kLogMinimum, "browse: the Newton asked to resolve an alias");
  });
  ioDock.handle(kDLoadPackageFile, [this](DockController & dock, const DockEvent & e) { loadPackageFile(dock, e); });
}


void FileBrowser::requestToBrowse(DockController & ioDock, const DockEvent & inEvent)
{
  // what it browses for: 'packages, 'import, 'syncFiles
  RefVar kind(EventRef(inEvent));
  std::string name = IsSymbol(kind) ? Lower(UTF8FromSymbol(kind)) : std::string();
  mKind = name == "packages" ? kPackages : name == "import" ? kImport : name == "syncfiles" ? kSyncFiles : kNothing;
  if (mKind == kImport) {
    // as the Cocoa app: the internal store first (its details aren't used)
    ioDock.session().send(kDGetInternalStore);
    ioDock.session().receive(kDInternalStore);
  }
  if (mFolder.empty())
    mFolder = HomeFolder();
  ioDock.session().sendValue(kDResult, 0);
}


void FileBrowser::setPath(DockController & ioDock, const DockEvent & inEvent)
{
  // [computer, disk, folder, ...]
  RefVar elements(EventRef(inEvent));
  if (IsArray(elements)) {
    fs::path folder;
    ArrayIndex count = Length(elements);
#if defined(_WIN32)
    if (count >= 2)
      folder = PathFromUTF8(ElementName(GetArraySlot(elements, 1)));     // "C:\"
#else
    folder = "/";
#endif
    for (ArrayIndex i = 2; i < count; ++i)
      folder /= PathFromUTF8(ElementName(GetArraySlot(elements, i)));
    std::error_code ec;
    if (!folder.empty() && fs::is_directory(folder, ec))
      mFolder = folder;
    else
      Log(kLogMinimum, "browse: no folder %s", UTF8FromPath(folder).c_str());
  }
  ioDock.session().sendRef(kDFilesAndFolders, RefVar(fileList()));
}


void FileBrowser::fileInfo(DockController & ioDock, const DockEvent & inEvent)
{
  fs::path file = this->file(inEvent);
  std::error_code ec;
  RefVar info(AllocateFrame());
  bool folder = fs::is_directory(file, ec);
  std::string kind = folder ? "Folder" : "File";
  if (!folder) {
    std::string extension = Lower(UTF8FromPath(file.extension()));
    if (IsNewtonPackage(file))
      kind = "Newton package";
    else if (extension == ".newtondevice")
      kind = "Newton Connection backup";
    else if (extension == ".nbku")
      kind = "Newton Connection 1 backup";
  }
  SetFrameSlot(info, SYMA(kind), MakeStringFromUTF8(kind));
  if (!folder) {
    uintmax_t size = fs::file_size(file, ec);
    if (!ec)
      SetFrameSlot(info, SYMA(size), MAKEINT((int64_t)std::min<uintmax_t>(size, 0x1FFFFFFF)));
  }
  fs::file_time_type modified = fs::last_write_time(file, ec);
  if (!ec) {
    // minutes since 1904; there is no portable creation date
    RefVar minutes(MAKEINT(NewtonFromTime(Seconds(modified))));
    SetFrameSlot(info, MakeSymbol("created"), minutes);
    SetFrameSlot(info, MakeSymbol("modified"), minutes);
  }
  SetFrameSlot(info, SYMA(path), MakeStringFromUTF8(UTF8FromPath(file)));
  SetFrameSlot(info, SYMA(icon), NILREF);
  ioDock.session().sendRef(kDFileInfo, info);
}


void FileBrowser::loadPackageFile(DockController & ioDock, const DockEvent & inEvent)
{
  fs::path file = this->file(inEvent);
  int error = PackageInstaller(ioDock).load(file);
  if (error != 0)
    Log(kLogMinimum, "browse: installing %s: %d", UTF8FromPath(file).c_str(), error);
  if (mInstalled)
    mInstalled(file, error);
}


bool FileBrowser::shows(const fs::path & inFile) const
{
  std::string extension = Lower(UTF8FromPath(inFile.extension()));
  switch (mKind) {
    case kPackages: return IsNewtonPackage(inFile);
    case kSyncFiles: return extension == ".newtondevice" || extension == ".nbku";
    case kImport: return mImportable && mImportable(inFile);
    default: return false;
  }
}


Ref FileBrowser::path() const
{
  RefVar elements(MakeArray(0));
  AddArraySlot(elements, RefVar(Element(ComputerName(), kDesktop)));
  fs::path folder = fs::absolute(mFolder);
  bool first = true;
  for (const fs::path & part : folder) {
    std::string name = UTF8FromPath(part);
    if (name.empty())
      continue;
#if defined(_WIN32)
    // "C:" and "\" are one disk: "C:\"
    if (first && folder.has_root_name()) {
      AddArraySlot(elements, RefVar(Element(UTF8FromPath(folder.root_path()), kDesktopDisk)));
      first = false;
      continue;
    }
    if (part == folder.root_directory())
      continue;
#endif
    AddArraySlot(elements, RefVar(Element(name, first ? kDesktopDisk : kDesktopFolder)));
    first = false;
  }
  return elements;
}


Ref FileBrowser::fileList() const
{
  struct Item { std::string name; int type; };
  std::vector<Item> items;
  std::error_code ec;
  for (const auto & entry : fs::directory_iterator(mFolder, fs::directory_options::skip_permission_denied, ec)) {
    std::string name = UTF8FromPath(entry.path().filename());
    if (name.empty() || name[0] == '.')
      continue;
    std::error_code typeError;
    if (entry.is_directory(typeError))
      items.push_back({ name, kDesktopFolder });
    else if (shows(entry.path()))
      items.push_back({ name, kDesktopFile });
  }
  std::sort(items.begin(), items.end(), [](const Item & a, const Item & b) { return Lower(a.name) < Lower(b.name); });
  RefVar list(MakeArray(0));
  for (const Item & item : items)
    AddArraySlot(list, RefVar(Element(item.name, item.type)));
  return list;
}


fs::path FileBrowser::file(const DockEvent & inEvent) const
{
  // its name: a string, or a frame with one
  RefVar name(EventRef(inEvent));
  return mFolder / PathFromUTF8(ElementName(name));
}

} // namespace ncx
