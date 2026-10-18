/*
 File: EntryPreview.cc

 An entry as the inspector shows it (EntryPreview.h).
 */

#include "ui/EntryPreview.h"

#include "base/NewtonCall.h"
#include "model/NewtonModel.h"
#include "platform/AppFolders.h"
#include "translators/Canvas.h"
#include "translators/Translator.h"

#include <cctype>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>

namespace ncx {

namespace {

const size_t kMaxText = 256 * 1024;       // more isn't shown

bool ReadFile(const std::filesystem::path & inFile, std::string & outData)
{
  std::ifstream in(inFile, std::ios::binary);
  if (!in)
    return false;
  outData.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

/** Text as paragraphs, a line each (CR, LF, CRLF). */
void AddText(RichDocument & ioDocument, const std::string & inText)
{
  TextStyle style;
  style.font = "Courier";
  style.size = 11;
  size_t start = 0;
  while (start <= inText.size()) {
    size_t end = inText.find_first_of("\r\n", start);
    std::string line = inText.substr(start, end == std::string::npos ? std::string::npos : end - start);
    RichParagraph paragraph;
    if (!line.empty())
      paragraph.addText(line, style);
    ioDocument.paragraphs.push_back(paragraph);
    if (end == std::string::npos)
      break;
    start = end + (inText.compare(end, 2, "\r\n") == 0 ? 2 : 1);
  }
}

/** What a file the translator wrote shows. */
void AddFile(EntryPreview & ioPreview, const std::filesystem::path & inFile)
{
  std::string extension = inFile.extension().string();
  for (char & c : extension)
    c = (char)std::tolower((unsigned char)c);
  std::error_code error;
  uintmax_t size = std::filesystem::is_regular_file(inFile, error) ? std::filesystem::file_size(inFile, error) : 0;
  std::string data;
  if (extension == ".png" && ReadFile(inFile, data)) {
    Pixels pixels = ReadPNG(inFile);
    RichParagraph paragraph;
    paragraph.addPicture(data, pixels.width, pixels.height);
    ioPreview.document.paragraphs.push_back(paragraph);
    ioPreview.format = "PNG";
  } else if ((extension == ".vcf" || extension == ".ics" || extension == ".txt" || extension == ".text"
              || extension == ".csv" || extension == ".rtf") && ReadFile(inFile, data)) {
    if (extension == ".rtf") {
      ioPreview.document = ReadRTF(data);
      ioPreview.format = "RTF";
      return;
    }
    if (data.size() > kMaxText)
      data.resize(kMaxText);
    AddText(ioPreview.document, data);
    ioPreview.monospaced = true;
    ioPreview.format = extension.substr(1);
  } else {
    RichParagraph paragraph;
    TextStyle style;
    paragraph.addText(UTF8FromPath(inFile.filename()) + " (" + std::to_string(size) + " bytes)", style);
    ioPreview.document.paragraphs.push_back(paragraph);
    ioPreview.format = extension.empty() ? std::string("file") : extension.substr(1);
  }
}

} // namespace


EntryPreview ReadEntryPreview(const std::filesystem::path & inFile, int64_t inEntryPk, const std::string & inAppName,
                              const std::filesystem::path & inFolder)
{
  EntryPreview preview;
  std::string error;
  std::unique_ptr<DeviceFile> file = DeviceFile::Open(inFile, DeviceFile::kReadOnly, error);
  if (!file) {
    preview.error = error;
    return preview;
  }
  std::error_code ec;
  std::filesystem::remove_all(inFolder, ec);
  std::filesystem::create_directories(inFolder, ec);
  try {
    std::optional<EntryRecord> entry = file->entry(inEntryPk, true);
    if (!entry) {
      preview.error = "the entry is gone";
      return preview;
    }
    bool rich = false;
    std::vector<std::filesystem::path> files;
    CallNewton([&] {
      RefVar frame(EntryFrame(*entry));
      if (!IsFrame(frame)) {
        preview.error = "the entry can't be read";
        return;
      }
      TranslatorContext context;
      context.folder = inFolder;
      context.appName = inAppName;
      context.richText = RichTextFormat::kRTF;
      context.richTextSink = [&](const RichDocument & inDocument) {
        preview.document = inDocument;
        preview.format = "RTF";
        rich = true;
      };
      ExportSession session(context);
      session.add(frame);
      files = session.end();
    });
    if (!rich)
      for (const std::filesystem::path & each : files)
        AddFile(preview, each);
    if (!rich && files.empty() && preview.error.empty())
      preview.error = "nothing to show";
  } catch (const std::exception & e) {
    preview.error = e.what();
  }
  std::filesystem::remove_all(inFolder, ec);
  return preview;
}

} // namespace ncx
