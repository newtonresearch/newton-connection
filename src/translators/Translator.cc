/*
 File: Translator.cc

 Translating soup entries to desktop files and back (Translator.h).
 */

#include "translators/Translator.h"
#include "translators/RichText.h"
#include "base/Dates.h"
#include "base/NewtonStrings.h"
#include "base/Strings.h"
#include "platform/AppFolders.h"

#include "RSSymbols.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <system_error>

namespace ncx {

/*-- Exporter, Importer ------------------------------------------------------*/

RichTextFormat DefaultRichTextFormat()
{
#if defined(__APPLE__)
  return RichTextFormat::kRTFDWithPictures;
#else
  return RichTextFormat::kRTF;
#endif
}


std::filesystem::path Exporter::freePath(const std::string & inName, const std::string & inExtension)
{
  // a name every file system takes: no path separators, no characters
  // Windows refuses, no control characters, no blanks around it
  std::string name;
  for (char c : inName)
    name += (std::strchr("/\\:*?\"<>|", c) != nullptr && c != 0) || (unsigned char)c < 0x20 ? '-' : c;
  size_t first = name.find_first_not_of(" \t.");
  size_t last = name.find_last_not_of(" \t.");
  name = first == std::string::npos ? std::string() : name.substr(first, last - first + 1);
  if (name.size() > 200)
    name.resize(200);                 // (a UTF-8 character may be cut: still a valid file name byte string)
  if (name.empty())
    name = "untitled";
  std::error_code ec;
  std::filesystem::create_directories(mContext.folder, ec);
  for (int sequence = 1; sequence < 1000; ++sequence) {
    std::string fileName = (sequence == 1 ? name : name + "-" + std::to_string(sequence)) + "." + inExtension;
    std::filesystem::path path = mContext.folder / PathFromUTF8(fileName);
    if (!std::filesystem::exists(path, ec))
      return path;
  }
  return std::filesystem::path();
}


namespace {

bool WriteFile(const std::filesystem::path & inPath, const std::string & inData)
{
  std::ofstream out(inPath, std::ios::binary);
  out.write(inData.data(), (std::streamsize)inData.size());
  return (bool)out;
}

} // namespace


std::filesystem::path Exporter::write(const std::string & inData, const std::string & inName, const std::string & inExtension)
{
  std::filesystem::path path = freePath(inName, inExtension);
  return !path.empty() && WriteFile(path, inData) ? path : std::filesystem::path();
}


std::filesystem::path Exporter::writeRichText(const RichDocument & inDocument, const std::string & inName)
{
  if (mContext.richTextSink) {
    mContext.richTextSink(inDocument);
    return std::filesystem::path();
  }
  bool rtfd = mContext.richText == RichTextFormat::kRTFD
              || (mContext.richText == RichTextFormat::kRTFDWithPictures && HasPictures(inDocument));
  if (!rtfd)
    return write(WriteRTF(inDocument), inName, "rtf");
  // a folder: TXT.rtf, the pictures
  std::vector<Attachment> attachments;
  std::string rtf = WriteRTF(inDocument, &attachments);
  std::filesystem::path folder = freePath(inName, "rtfd");
  std::error_code ec;
  if (folder.empty() || !std::filesystem::create_directory(folder, ec))
    return std::filesystem::path();
  bool written = WriteFile(folder / "TXT.rtf", rtf);
  for (const Attachment & attachment : attachments)
    written = written && WriteFile(folder / attachment.name, attachment.png);
  return written ? folder : std::filesystem::path();
}


bool Importer::begin(const std::filesystem::path & inFile, const TranslatorContext & inContext)
{
  mFile = inFile;
  mContext = inContext;
  mWhy.clear();
  return true;
}


/*-- the tables --------------------------------------------------------------*/

std::string ExportClassOf(RefArg inEntry)
{
  if (!IsFrame(inEntry))
    return std::string();
  RefVar cls(GetFrameSlot(inEntry, SYMA(class)));
  if (IsSymbol(cls)) {
    std::string name = UTF8FromSymbol(cls);
    std::transform(name.begin(), name.end(), name.begin(), [](char c) { return (char)std::tolower((unsigned char)c); });
    return name;
  }
  // no class: what the Cocoa app made of it
  RefVar name;
  if (FrameHasSlot(inEntry, MakeSymbol("pkgRef")))
    return "*package*";
  if (NOTNIL(GetFrameSlot(inEntry, SYMA(company))))
    return "company";
  if (NOTNIL(name = GetFrameSlot(inEntry, SYMA(name))) && EQRef(ClassOf(name), SYMA(person)))
    return "person";
  if (IsFrame(GetFrameSlot(inEntry, MakeSymbol("image"))))
    return "*image*";
  if (NOTNIL(GetFrameSlot(inEntry, SYMA(text))))
    return "text";
  return std::string();
}


const ExportFormat * FindExportFormat(const std::string & inEntryClass)
{
  for (const ExportFormat & format : ExportFormats())
    if (format.entryClass == inEntryClass)
      return &format;
  return nullptr;
}


namespace {

std::string ExtensionOf(const std::filesystem::path & inFile)
{
  std::string extension = UTF8FromPath(inFile.extension());
  if (!extension.empty() && extension[0] == '.')
    extension.erase(0, 1);
  std::transform(extension.begin(), extension.end(), extension.begin(), [](char c) { return (char)std::tolower((unsigned char)c); });
  return extension;
}

} // namespace


std::vector<std::string> ImportAppsFor(const std::filesystem::path & inFile)
{
  std::string extension = ExtensionOf(inFile);
  std::vector<std::string> apps;
  for (const ImportFormat & format : ImportFormats())
    if (std::find(format.extensions.begin(), format.extensions.end(), extension) != format.extensions.end()
        && std::find(apps.begin(), apps.end(), format.app) == apps.end())
      apps.push_back(format.app);
  std::sort(apps.begin(), apps.end());
  return apps;
}


const ImportFormat * FindImportFormat(const std::string & inApp, const std::filesystem::path & inFile)
{
  std::string extension = ExtensionOf(inFile);
  for (const ImportFormat & format : ImportFormats())
    if (format.app == inApp && std::find(format.extensions.begin(), format.extensions.end(), extension) != format.extensions.end())
      return &format;
  return nullptr;
}


/*-- ExportSession -----------------------------------------------------------*/

std::unique_ptr<Exporter> MakeDefaultExporter();        // Builtin.cc


Exporter & ExportSession::exporterFor(RefArg inEntry)
{
  const ExportFormat * format = FindExportFormat(ExportClassOf(inEntry));
  std::string key = format ? format->translator : std::string();
  auto found = mByTranslator.find(key);
  if (found != mByTranslator.end())
    return *found->second;
  // one exporter per translator, begun when it's first needed
  std::unique_ptr<Exporter> exporter = format ? format->make() : MakeDefaultExporter();
  exporter->begin(mContext);
  Exporter * result = exporter.get();
  mExporters.push_back(std::move(exporter));
  mByTranslator[key] = result;
  return *result;
}


void ExportSession::add(RefArg inEntry)
{
  std::filesystem::path file = exporterFor(inEntry).exportEntry(inEntry);
  if (!file.empty())
    mFiles.push_back(file);
}


std::vector<std::filesystem::path> ExportSession::end()
{
  for (const std::unique_ptr<Exporter> & exporter : mExporters) {
    std::filesystem::path file = exporter->end();
    if (!file.empty())
      mFiles.push_back(file);
  }
  mExporters.clear();
  mByTranslator.clear();
  return mFiles;
}


/*-- helpers -----------------------------------------------------------------*/

Ref MakeBounds(int inLeft, int inTop, int inRight, int inBottom)
{
  RefVar bounds(AllocateFrame());
  SetFrameSlot(bounds, SYMA(left), MAKEINT(inLeft));
  SetFrameSlot(bounds, SYMA(top), MAKEINT(inTop));
  SetFrameSlot(bounds, SYMA(right), MAKEINT(inRight));
  SetFrameSlot(bounds, SYMA(bottom), MAKEINT(inBottom));
  return bounds;
}


Ref MakeParagraph(const std::string & inText, int inLeft, int inTop, int inRight, int inBottom, int64_t inUserFont)
{
  RefVar para(AllocateFrame());
  SetFrameSlot(para, SYMA(viewStationery), MakeSymbol("para"));
  SetFrameSlot(para, SYMA(viewBounds), RefVar(MakeBounds(inLeft, inTop, inRight, inBottom)));
  SetFrameSlot(para, SYMA(text), RefVar(MakeStringFromUTF8(inText)));
  if (inUserFont)
    SetFrameSlot(para, SYMA(viewFont), MAKEINT(inUserFont));
  return para;
}


std::string ParagraphsText(RefArg inData)
{
  std::string text;
  for (ArrayIndex i = 0, count = IsArray(inData) ? Length(inData) : 0; i < count; ++i) {
    RefVar para(GetArraySlot(inData, i));
    if (!IsFrame(para) || !EQRef(GetFrameSlot(para, SYMA(viewStationery)), MakeSymbol("para")))
      continue;
    std::string paraText = StringSlot(para, "text");
    text += (text.empty() ? "" : "\n") + paraText;
  }
  return text;
}


std::string StringSlot(RefArg inFrame, const char * inSlot)
{
  if (!IsFrame(inFrame))
    return std::string();
  RefVar value(GetFrameSlot(inFrame, MakeSymbol(inSlot)));
  return IsString(value) ? UTF8FromStringLF(value) : std::string();
}


std::string FullName(RefArg inName)
{
  std::string first = StringSlot(inName, "first"), last = StringSlot(inName, "last");
  return first.empty() ? last : last.empty() ? first : first + " " + last;
}


std::string EntryFileName(RefArg inEntry)
{
  std::string title = StringSlot(inEntry, "title");
  if (!title.empty())
    return title;
  RefVar timestamp(IsFrame(inEntry) ? GetFrameSlot(inEntry, SYMA(timestamp)) : (Ref)NILREF);
  if (ISINT(timestamp))
    return FormatDate(TimeFromNewton(RINT(timestamp)));
  return "untitled";
}

} // namespace ncx
