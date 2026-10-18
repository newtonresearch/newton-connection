/*
 File: PackageInfo.cc

 What a package says of itself (PackageInfo.h).
 */

#include "ui/PackageInfo.h"
#include "ui/Format.h"

#include "base/Dates.h"
#include "base/NewtonCall.h"
#include "base/NewtonStrings.h"
#include "base/Strings.h"
#include "model/NewtonModel.h"
#include "translators/Render.h"

#include "NewtonPackage.h"

#include <cstdlib>
#include <cstring>
#include <exception>

namespace ncx {

namespace {

const int64_t kSeconds1904to1970 = 2082844800;

/** A string in the directory's data area (UTF-16, platform order, maybe
    with its null at the end). */
std::string DirectoryString(const PackageDirectory * inDir, const InfoRef & inRef)
{
  if (inRef.length < 2)
    return std::string();
  const char * data = (const char *)inDir + sizeof(PackageDirectory) + inDir->numParts * sizeof(PartEntry);
  const uint16_t * chars = (const uint16_t *)(data + inRef.offset);
  size_t count = inRef.length / 2;
  while (count > 0 && chars[count - 1] == 0)
    --count;
  return UTF8FromUTF16(chars, count);
}

std::string FourCC(uint32_t inType)
{
  std::string text;
  for (int shift = 24; shift >= 0; shift -= 8) {
    char c = (char)((inType >> shift) & 0xFF);
    text += (c >= 32 && c < 127) ? c : '?';
  }
  return text;
}

std::string StringSlot(RefArg inFrame, const char * inSlot)
{
  if (!IsFrame(inFrame))
    return std::string();
  RefVar value(GetFrameSlot(inFrame, MakeSymbol(inSlot)));
  return IsString(value) ? UTF8FromString(value) : std::string();
}

} // namespace


PackageInfo ReadPackageInfo(const std::vector<uint8_t> & inPackage)
{
  PackageInfo info;
  if (inPackage.size() < sizeof(PackageDirectory) || std::memcmp(inPackage.data(), "package", 7) != 0) {
    info.error = "not a package";
    return info;
  }
  // NewtonPackage reads from the bytes, it doesn't change them
  std::vector<uint8_t> bytes(inPackage);
  try {
    CallNewton([&] {
      NewtonPackage package(bytes.data());
      const PackageDirectory * dir = package.directory();
      if (!dir) {
        info.error = "not a package";
        return;
      }
      info.name = DirectoryString(dir, dir->name);
      info.copyright = DirectoryString(dir, dir->copyright);
      info.ident = std::to_string(dir->id);
      info.version = std::to_string(dir->version);
      info.size = GroupedNumber(dir->size) + " bytes";
      if (dir->creationDate != 0)     // (newtc leaves it 0)
        info.created = FormatDateTime((int64_t)dir->creationDate - kSeconds1904to1970);
      info.copyProtected = (dir->flags & kCopyProtectFlag) != 0;
      for (uint32_t n = 0; n < dir->numParts; ++n) {
        const PartEntry * part = package.partEntry(n);
        PackagePartInfo partInfo;
        uint32_t kind = part->flags & kPartTypeMask;
        std::string type = part->type ? FourCC(part->type) : std::string();
        if (kind == kProtocolPart)
          type = type.empty() ? std::string("protocol") : type + " protocol";
        else if (kind == kRawPart)
          type = type.empty() ? std::string("raw") : "raw " + type;
        if (type.empty())
          type = "unspecified type";
        partInfo.title = "Part " + std::to_string(n) + "  (" + type + ")";
        partInfo.size = GroupedNumber(part->size) + " bytes";
        if (kind == kNOSPart) {
          RefVar root(package.partRef(n));
          if (part->type == 'form' || part->type == 'auto') {
            partInfo.form = true;
            partInfo.text = StringSlot(root, "text");
            RefVar icon;
            if (IsFrame(root)) {
              RefVar iconPro(GetFrameSlot(root, MakeSymbol("iconPro")));
              if (IsFrame(iconPro))
                icon = GetFrameSlot(iconPro, MakeSymbol("unhilited"));
              if (ISNIL(icon))
                icon = GetFrameSlot(root, MakeSymbol("icon"));
            }
            if (IsFrame(icon))
              partInfo.icon = RenderBitmap(icon);
          } else if (part->type == 'book') {
            partInfo.book = true;
            RefVar book(IsFrame(root) ? GetFrameSlot(root, MakeSymbol("book")) : (Ref)NILREF);
            partInfo.bookTitle = StringSlot(book, "title");
            partInfo.isbn = StringSlot(book, "ISBN");
            partInfo.author = StringSlot(book, "author");
            partInfo.copyright = StringSlot(book, "copyright");
            RefVar date(IsFrame(book) ? GetFrameSlot(book, MakeSymbol("publicationDate")) : (Ref)NILREF);
            if (ISINT(date))
              partInfo.published = FormatDate(TimeFromNewton(RINT(date)));
          }
          // the part's objects live outside the heap: done with them
          root = NILREF;
          if (MemAllocation * data = package.partPkgData(n))
            if (data->data) {
              std::free(data->data);
              data->data = nullptr;
            }
        }
        info.parts.push_back(partInfo);
      }
    });
  } catch (const std::exception & e) {
    info.error = e.what();
  }
  return info;
}


PackageInfo ReadPackageEntryInfo(const std::filesystem::path & inFile, int64_t inEntryPk)
{
  PackageInfo info;
  std::string error;
  std::unique_ptr<DeviceFile> file = DeviceFile::Open(inFile, DeviceFile::kReadOnly, error);
  if (!file) {
    info.error = error;
    return info;
  }
  std::vector<uint8_t> package;
  try {
    std::optional<EntryRecord> entry = file->entry(inEntryPk, true);
    if (!entry) {
      info.error = "the entry is gone";
      return info;
    }
    CallNewton([&] {
      RefVar frame(EntryFrame(*entry));
      RefVar pkgRef(IsFrame(frame) ? GetFrameSlot(frame, MakeSymbol("pkgRef")) : (Ref)NILREF);
      if (IsBinary(pkgRef)) {
        CDataPtr data(pkgRef);
        package.assign((const uint8_t *)(char *)data, (const uint8_t *)(char *)data + Length(pkgRef));
      }
    });
  } catch (const std::exception & e) {
    info.error = e.what();
    return info;
  }
  if (package.empty()) {
    info.error = "not a package entry";
    return info;
  }
  return ReadPackageInfo(package);
}

} // namespace ncx
