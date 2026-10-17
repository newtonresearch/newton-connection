/*
 File: Notes.cc

 Notes from text files (the Cocoa app's NoteFromText): a note of about 4 KB
 of text each, broken between words, titled after the file ("Letter",
 "Letter (2)", ...):

   {class: 'paperroll, viewStationery: 'paperroll, title, height: 500,
    timestamp, _version: 2,
    data: [{viewStationery: 'para, viewBounds: {5, 5, 315, 505}, text}]}

 (the ROM's template for it is magic pointer 382). The file may be UTF-8
 (with or without a BOM), UTF-16 with a BOM, or else Latin-1. Notes to RTF,
 and from RTF, come with the rich text translators (PORTING.md 6.4).
 */

#include "translators/Translator.h"
#include "base/Dates.h"
#include "base/NewtonStrings.h"
#include "base/Strings.h"
#include "platform/AppFolders.h"

#include "RSSymbols.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <memory>

namespace ncx {

namespace {

bool IsUTF8(const std::string & inText)
{
  for (size_t i = 0; i < inText.size();) {
    unsigned char c = (unsigned char)inText[i];
    size_t more = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 9;
    if (more == 9 || i + more >= inText.size() + (more ? 0 : 1))
      return false;
    for (size_t k = 1; k <= more; ++k)
      if (((unsigned char)inText[i + k] & 0xC0) != 0x80)
        return false;
    i += more + 1;
  }
  return true;
}

/** A text file's bytes as UTF-8. */
std::string TextAsUTF8(const std::string & inBytes)
{
  if (inBytes.size() >= 3 && inBytes.compare(0, 3, "\xEF\xBB\xBF") == 0)
    return inBytes.substr(3);
  if (inBytes.size() >= 2 && ((unsigned char)inBytes[0] == 0xFF || (unsigned char)inBytes[0] == 0xFE)
      && ((unsigned char)inBytes[1] == 0xFE || (unsigned char)inBytes[1] == 0xFF) && inBytes[0] != inBytes[1]) {
    bool little = (unsigned char)inBytes[0] == 0xFF;
    std::u16string text;
    for (size_t i = 2; i + 1 < inBytes.size(); i += 2) {
      unsigned char a = (unsigned char)inBytes[i], b = (unsigned char)inBytes[i + 1];
      text += (char16_t)(little ? (b << 8 | a) : (a << 8 | b));
    }
    return UTF8FromUTF16(text);
  }
  if (IsUTF8(inBytes))
    return inBytes;
  std::string text;          // Latin-1
  for (unsigned char c : inBytes) {
    if (c < 0x80) {
      text += (char)c;
    } else {
      text += (char)(0xC0 | c >> 6);
      text += (char)(0x80 | (c & 0x3F));
    }
  }
  return text;
}


class NoteFromText : public Importer
{
public:
  static constexpr size_t kChunk = 4096;

  bool begin(const std::filesystem::path & inFile, const TranslatorContext & inContext) override
  {
    Importer::begin(inFile, inContext);
    std::ifstream in(inFile, std::ios::binary);
    if (!in) {
      mWhy = "can't read the file";
      return false;
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    mText = TextAsUTF8(bytes);
    mTitle = UTF8FromPath(inFile.stem());
    return true;
  }

  Ref next(std::string &) override
  {
    if (mAt >= mText.size())
      return NILREF;
    // about 4 KB, between words (or at least not inside a character)
    size_t length = mText.size() - mAt;
    if (length > kChunk) {
      size_t cut = kChunk;
      while (cut > 0 && !std::isspace((unsigned char)mText[mAt + cut]))
        --cut;
      if (cut == 0) {
        cut = kChunk;
        while (cut > 0 && ((unsigned char)mText[mAt + cut] & 0xC0) == 0x80)
          --cut;
      }
      length = cut;
    }
    std::string text = mText.substr(mAt, length);
    mAt += length;
    ++mIndex;

    RefVar data(MakeArray(1));
    SetArraySlot(data, 0, RefVar(MakeParagraph(text, 5, 5, 315, 505, mContext.userFont)));
    RefVar entry(AllocateFrame());
    SetFrameSlot(entry, SYMA(class), MakeSymbol("paperroll"));
    SetFrameSlot(entry, SYMA(viewStationery), MakeSymbol("paperroll"));
    SetFrameSlot(entry, SYMA(title), RefVar(MakeStringFromUTF8(mIndex > 1 ? mTitle + " (" + std::to_string(mIndex) + ")" : mTitle)));
    SetFrameSlot(entry, SYMA(data), data);
    SetFrameSlot(entry, SYMA(height), MAKEINT(500));
    SetFrameSlot(entry, SYMA(timestamp), MAKEINT(NewtonFromTime(Now())));
    SetFrameSlot(entry, MakeSymbol("_version"), MAKEINT(2));
    return entry;
  }

private:
  std::string mText;
  std::string mTitle;
  size_t mAt = 0;
  int mIndex = 0;
};

} // namespace


std::unique_ptr<Importer> MakeNoteFromText() { return std::make_unique<NoteFromText>(); }

} // namespace ncx
