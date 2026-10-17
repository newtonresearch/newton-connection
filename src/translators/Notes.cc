/*
 File: Notes.cc

 Notes from text files (the Cocoa app's NoteFromText): a note of about 4 KB
 of text each, broken between words, titled after the file ("Letter",
 "Letter (2)", ...):

   {class: 'paperroll, viewStationery: 'paperroll, title, height: 500,
    timestamp, _version: 2,
    data: [{viewStationery: 'para, viewBounds: {5, 5, 315, 505}, text}]}

 (the ROM's template for it is magic pointer 382). The file may be UTF-8
 (with or without a BOM), UTF-16 with a BOM, or else Latin-1.

 Notes as RTF (the Cocoa app's NoteToRTF; PORTING.md 6.4), a file each:
   - a note ('paperroll) or graph paper ('|graphPaper:RSM|, its editView):
     its paragraphs top to bottom with their styles, ink words as pictures;
     its graphics (ink, shapes, pictures) as one picture, where they begin
   - a checklist or an outline ('checklist, 'list): its topics, indented
     by their level, with a box (checked if done) or a bullet
 And notes from RTF (NoteFromRTF), as from text, keeping the fonts, sizes
 and faces; pictures are left out.
 */

#include "translators/Translator.h"
#include "translators/NewtonText.h"
#include "translators/Render.h"
#include "translators/RichText.h"
#include "base/Dates.h"
#include "base/NewtonStrings.h"
#include "base/Strings.h"
#include "platform/AppFolders.h"

#include "RSSymbols.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <algorithm>
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


/*-- RTF ---------------------------------------------------------------------*/

int TopOf(RefArg inParagraph)
{
  RefVar bounds(GetFrameSlot(inParagraph, SYMA(viewBounds)));
  RefVar top(IsFrame(bounds) ? GetFrameSlot(bounds, SYMA(top)) : (Ref)NILREF);
  return ISINT(top) ? (int)RINT(top) : 0;
}


/** A note's paragraphs, top to bottom, its graphics as one picture before
    the first text below where they begin. */
void AddParagraphs(RichDocument & ioDocument, RefArg inParagraphs)
{
  if (!IsArray(inParagraphs))
    return;
  std::vector<ArrayIndex> order;
  for (ArrayIndex i = 0; i < Length(inParagraphs); ++i)
    order.push_back(i);
  std::stable_sort(order.begin(), order.end(), [&](ArrayIndex a, ArrayIndex b) {
    return TopOf(RefVar(GetArraySlot(inParagraphs, a))) < TopOf(RefVar(GetArraySlot(inParagraphs, b)));
  });
  int graphicsLeft = 0, graphicsTop = 0;
  Pixels graphics = RenderGraphics(inParagraphs, &graphicsLeft, &graphicsTop);
  bool graphicsDone = graphics.empty();
  auto addGraphics = [&] {
    RichParagraph para;
    para.addPicture(PNGData(graphics), graphics.width, graphics.height);
    ioDocument.paragraphs.push_back(para);
    graphicsDone = true;
  };
  for (ArrayIndex i : order) {
    RefVar para(GetArraySlot(inParagraphs, i));
    if (!IsFrame(para) || !EQRef(GetFrameSlot(para, SYMA(viewStationery)), MakeSymbol("para")))
      continue;
    RefVar text(GetFrameSlot(para, SYMA(text)));
    if (!IsString(text))
      continue;
    if (!graphicsDone && TopOf(para) >= graphicsTop)
      addGraphics();
    TextStyle style = StyleOfFontSpec(RefVar(GetFrameSlot(para, SYMA(viewFont))));
    AddStyledText(ioDocument, UTF16Of(text), RefVar(GetFrameSlot(para, SYMA(styles))), style);
  }
  if (!graphicsDone)
    addGraphics();
}


/** A checklist's or an outline's topics. */
void AddTopics(RichDocument & ioDocument, RefArg inTopics, bool inChecklist)
{
  for (ArrayIndex i = 0, n = IsArray(inTopics) ? Length(inTopics) : 0; i < n; ++i) {
    RefVar topic(GetArraySlot(inTopics, i));
    RefVar text(GetFrameSlot(topic, SYMA(text)));
    if (!IsString(text))
      continue;
    RefVar level(GetFrameSlot(topic, MakeSymbol("level")));
    int indent = ISINT(level) && RINT(level) > 1 ? (int)RINT(level) - 1 : 0;
    // the box or bullet: U+2610/U+2611, U+25E6
    std::string mark = inChecklist ? (NOTNIL(GetFrameSlot(topic, MakeSymbol("mtgDone"))) ? "\xE2\x98\x91 " : "\xE2\x98\x90 ")
                                   : "\xE2\x97\xA6 ";
    RichParagraph para;
    para.style.leftIndent = 18 * indent + 18;
    para.style.firstIndent = -18;
    para.addText(mark, TextStyle());
    ioDocument.paragraphs.push_back(para);
    AddStyledText(ioDocument, UTF16Of(text), RefVar(GetFrameSlot(topic, SYMA(styles))), TextStyle(), nullptr, false);
  }
}


class NoteToRTF : public Exporter
{
public:
  std::filesystem::path exportEntry(RefArg inEntry) override
  {
    RichDocument document;
    RefVar cls(ClassOf(inEntry));
    if (EQRef(cls, MakeSymbol("paperroll")))
      AddParagraphs(document, RefVar(GetFrameSlot(inEntry, SYMA(data))));
    else if (EQRef(cls, MakeSymbol("checklist")) || EQRef(cls, MakeSymbol("list")))
      AddTopics(document, RefVar(GetFrameSlot(inEntry, MakeSymbol("topics"))), EQRef(cls, MakeSymbol("checklist")));
    else if (EQRef(cls, MakeSymbol("graphPaper:RSM"))) {
      RefVar paper(GetFrameSlot(inEntry, MakeSymbol("graphPaper:RSM")));
      AddParagraphs(document, RefVar(IsFrame(paper) ? GetFrameSlot(paper, MakeSymbol("editView")) : (Ref)NILREF));
    } else
      return std::filesystem::path();
    return write(WriteRTF(document), EntryFileName(inEntry), "rtf");
  }
};


class NoteFromRTF : public Importer
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
    std::string rtf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    mDocument = ReadRTF(rtf);
    mTitle = UTF8FromPath(inFile.stem());
    return true;
  }

  Ref next(std::string &) override
  {
    // about 4 KB a note, whole paragraphs (a longer one alone)
    if (mAt >= mDocument.paragraphs.size())
      return NILREF;
    std::vector<RichParagraph> part;
    size_t size = 0;
    while (mAt < mDocument.paragraphs.size()) {
      size_t length = UTF16FromUTF8(mDocument.paragraphs[mAt].text()).size() + 1;
      if (!part.empty() && size + length > kChunk)
        break;
      part.push_back(mDocument.paragraphs[mAt++]);
      size += length;
    }
    ++mIndex;
    std::u16string text;
    RefVar styles;
    NewtonStyledText(part, text, styles);
    RefVar para(MakeParagraph(UTF8FromUTF16(text), 5, 5, 315, 505, mContext.userFont));
    if (Length(styles) > 0)
      SetFrameSlot(para, SYMA(styles), styles);
    RefVar data(MakeArray(1));
    SetArraySlot(data, 0, para);
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
  RichDocument mDocument;
  std::string mTitle;
  size_t mAt = 0;
  int mIndex = 0;
};

} // namespace


std::unique_ptr<Importer> MakeNoteFromText() { return std::make_unique<NoteFromText>(); }
std::unique_ptr<Exporter> MakeNoteToRTF() { return std::make_unique<NoteToRTF>(); }
std::unique_ptr<Importer> MakeNoteFromRTF() { return std::make_unique<NoteFromRTF>(); }

} // namespace ncx
