/*
 File: RichText.cc

 Styled text with pictures, and RTF (RichText.h).
 */

#include "translators/RichText.h"
#include "base/Strings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace ncx {

bool TextStyle::operator==(const TextStyle & o) const
{
  return family == o.family && font == o.font && size == o.size && bold == o.bold && italic == o.italic && underline == o.underline
         && outline == o.outline && superscript == o.superscript && subscript == o.subscript;
}


bool ParagraphStyle::operator==(const ParagraphStyle & o) const
{
  if (alignment != o.alignment || firstIndent != o.firstIndent || leftIndent != o.leftIndent || rightIndent != o.rightIndent
      || lineSpacing != o.lineSpacing || tabs.size() != o.tabs.size())
    return false;
  for (size_t i = 0; i < tabs.size(); ++i)
    if (tabs[i].position != o.tabs[i].position || tabs[i].kind != o.tabs[i].kind)
      return false;
  return true;
}


void RichParagraph::addText(const std::string & inText, const TextStyle & inStyle)
{
  if (inText.empty())
    return;
  if (!runs.empty() && runs.back().png.empty() && runs.back().style == inStyle)
    runs.back().text += inText;
  else {
    TextRun run;
    run.text = inText;
    run.style = inStyle;
    runs.push_back(run);
  }
}


void RichParagraph::addPicture(const std::string & inPNG, int inWidth, int inHeight)
{
  if (inPNG.empty())
    return;
  TextRun run;
  run.png = inPNG;
  run.width = inWidth;
  run.height = inHeight;
  runs.push_back(run);
}


std::string RichParagraph::text() const
{
  std::string text;
  for (const TextRun & run : runs)
    text += run.text;
  return text;
}


std::string RichDocument::text() const
{
  std::string text;
  for (size_t i = 0; i < paragraphs.size(); ++i)
    text += (i ? "\n" : "") + paragraphs[i].text();
  return text;
}


/*-- fonts -------------------------------------------------------------------*/

namespace {

// (the Cocoa app's MakeNSFont: Helvetica Neue, Hoefler Text, Geneva, Casual)
const struct { const char * name; const char * rtfFamily; } kFonts[] = {
  { "Helvetica Neue", "fswiss" }, { "Hoefler Text", "froman" }, { "Geneva", "fswiss" }, { "Casual", "fscript" } };

std::string Lower(std::string inText)
{
  for (char & c : inText)
    c = (char)std::tolower((unsigned char)c);
  return inText;
}

} // namespace


const char * FontName(FontFamily inFamily)
{
  return kFonts[(int)inFamily].name;
}


FontFamily FamilyOfFont(const std::string & inName, const std::string & inRTFFamily)
{
  std::string name = Lower(inName);
  if (name.find("casual") != std::string::npos || name.find("comic") != std::string::npos || inRTFFamily == "fscript")
    return FontFamily::kHandwriting;
  if (name.find("geneva") != std::string::npos)
    return FontFamily::kSimple;
  if (name.find("times") != std::string::npos || name.find("hoefler") != std::string::npos || name.find("georgia") != std::string::npos
      || name.find("new york") != std::string::npos || name.find("serif") != std::string::npos
      || (inRTFFamily == "froman" && name.find("sans") == std::string::npos))
    return FontFamily::kFancy;
  return FontFamily::kSystem;
}


/*-- RTF out -----------------------------------------------------------------*/

namespace {

/** Text for RTF: \ { } escaped, tabs, ASCII as it is, the rest \uN?
    (a signed 16-bit number; characters beyond, as two). */
std::string RTFText(const std::string & inText)
{
  std::string out;
  std::u16string text = UTF16FromUTF8(inText);
  for (char16_t c : text) {
    if (c == '\\' || c == '{' || c == '}') {
      out += '\\';
      out += (char)c;
    } else if (c == '\t') {
      out += "\\tab ";
    } else if (c >= 0x20 && c < 0x7F) {
      out += (char)c;
    } else if (c < 0x20) {
      // other control characters: none in RTF text
    } else {
      out += "\\u" + std::to_string((int)(int16_t)c) + "?";
    }
  }
  return out;
}

std::string Hex(const std::string & inBytes)
{
  static const char kDigits[] = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < inBytes.size(); ++i) {
    unsigned char b = (unsigned char)inBytes[i];
    out += kDigits[b >> 4];
    out += kDigits[b & 15];
    if (i % 64 == 63)
      out += "\n";
  }
  return out;
}

} // namespace


bool HasPictures(const RichDocument & inDocument)
{
  for (const RichParagraph & para : inDocument.paragraphs)
    for (const TextRun & run : para.runs)
      if (!run.png.empty())
        return true;
  return false;
}


std::string WriteRTF(const RichDocument & inDocument, std::vector<Attachment> * outAttachments)
{
  // the fonts used, numbered as they come: a family's, or one of its own
  auto nameOf = [](const TextStyle & inStyle) { return inStyle.font.empty() ? std::string(FontName(inStyle.family)) : inStyle.font; };
  std::vector<TextStyle> fonts;
  auto fontIndex = [&](const TextStyle & inStyle) {
    for (size_t i = 0; i < fonts.size(); ++i)
      if (nameOf(fonts[i]) == nameOf(inStyle))
        return (int)i;
    fonts.push_back(inStyle);
    return (int)fonts.size() - 1;
  };
  for (const RichParagraph & para : inDocument.paragraphs)
    for (const TextRun & run : para.runs)
      if (run.png.empty())
        fontIndex(run.style);
  if (fonts.empty())
    fonts.push_back(TextStyle());

  std::string rtf = "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n{\\fonttbl";
  for (size_t i = 0; i < fonts.size(); ++i) {
    std::string name = nameOf(fonts[i]), lower = Lower(name);
    const char * rtfFamily = kFonts[(int)fonts[i].family].rtfFamily;
    if (!fonts[i].font.empty())
      rtfFamily = lower.find("courier") != std::string::npos || lower.find("mono") != std::string::npos ? "fmodern"
                : FamilyOfFont(name) == FontFamily::kFancy ? "froman" : rtfFamily;
    rtf += "{\\f" + std::to_string(i) + "\\" + rtfFamily + "\\fcharset0 " + name + ";}";
  }
  rtf += "}\n";
  static const char * const kMarginWords[4] = { "\\margl", "\\margt", "\\margr", "\\margb" };
  bool margins = false;
  for (int i = 0; i < 4; ++i)
    if (inDocument.margins[i] >= 0) {
      rtf += kMarginWords[i] + std::to_string(inDocument.margins[i] * 20);
      margins = true;
    }
  if (margins)
    rtf += "\n";

  for (const RichParagraph & para : inDocument.paragraphs) {
    const ParagraphStyle & ps = para.style;
    rtf += "\\pard";
    static const char * const kAlign[] = { "\\ql", "\\qc", "\\qr", "\\qj" };
    rtf += kAlign[(int)ps.alignment];
    if (ps.leftIndent)
      rtf += "\\li" + std::to_string(ps.leftIndent * 20);
    if (ps.firstIndent)
      rtf += "\\fi" + std::to_string(ps.firstIndent * 20);
    if (ps.rightIndent)
      rtf += "\\ri" + std::to_string(ps.rightIndent * 20);
    if (ps.lineSpacing != 1.0)
      rtf += "\\sl" + std::to_string((int)(ps.lineSpacing * 240)) + "\\slmult1";
    for (const TabStop & tab : ps.tabs) {
      static const char * const kTabKinds[] = { "", "\\tqc", "\\tqr", "\\tqdec" };
      rtf += std::string(kTabKinds[tab.kind]) + "\\tx" + std::to_string(tab.position * 20);
    }
    rtf += " ";
    for (const TextRun & run : para.runs) {
      if (!run.png.empty() && outAttachments) {
        // RTFD: a file of the bundle, then the attachment character (0xAC)
        Attachment attachment;
        attachment.name = "image" + std::to_string(outAttachments->size() + 1) + ".png";
        attachment.png = run.png;
        outAttachments->push_back(attachment);
        rtf += "{{\\NeXTGraphic " + attachment.name + " \\width" + std::to_string(run.width * 20) + " \\height"
               + std::to_string(run.height * 20) + " \\appleattachmentpadding0 \\appleembedtype0 \\appleaqc\n}\xAC}";
        continue;
      }
      if (!run.png.empty()) {
        rtf += "{\\pict\\pngblip\\picw" + std::to_string(run.width) + "\\pich" + std::to_string(run.height) + "\\picwgoal"
               + std::to_string(run.width * 20) + "\\pichgoal" + std::to_string(run.height * 20) + "\n" + Hex(run.png) + "}";
        continue;
      }
      const TextStyle & s = run.style;
      rtf += "{\\f" + std::to_string(fontIndex(s)) + "\\fs" + std::to_string(s.size * 2);
      if (s.bold)
        rtf += "\\b";
      if (s.italic)
        rtf += "\\i";
      if (s.underline)
        rtf += "\\ul";
      if (s.outline)
        rtf += "\\outl";
      if (s.superscript)
        rtf += "\\super";
      if (s.subscript)
        rtf += "\\sub";
      rtf += " " + RTFText(run.text) + "}";
    }
    rtf += "\\par\n";
  }
  return rtf + "}\n";
}


/*-- RTF in ------------------------------------------------------------------*/

namespace {

/** Windows-1252's characters 0x80 ... 0x9F (the rest is Latin-1). */
char16_t CP1252(unsigned char inByte)
{
  static const char16_t k80[32] = {
    0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
    0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };
  return inByte >= 0x80 && inByte < 0xA0 ? k80[inByte - 0x80] : (char16_t)inByte;
}

struct ReaderState
{
  TextStyle style;
  int font = 0;
  int uc = 1;                     // characters after \uN to skip
  bool skip = false;              // a destination that isn't text
  bool fontTable = false;
};

class RTFReader
{
public:
  explicit RTFReader(const std::string & inRTF) : mIn(inRTF) { }

  RichDocument read()
  {
    mStack.push_back(ReaderState());
    mParagraph.style = ParagraphStyle();
    while (mAt < mIn.size()) {
      char c = mIn[mAt];
      if (c == '{') {
        ++mAt;
        flushText();
        mStack.push_back(mStack.back());
        mDestinationStart = true;
      } else if (c == '}') {
        ++mAt;
        flushText();
        if (mStack.back().fontTable && !mFontName.empty())
          endFont();
        if (mStack.size() > 1)
          mStack.pop_back();
        mDestinationStart = false;
      } else if (c == '\\') {
        controlWord();
      } else if (c == '\r' || c == '\n') {
        ++mAt;
      } else {
        ++mAt;
        mDestinationStart = false;
        addChar((unsigned char)c);
      }
    }
    flushText();
    if (!mParagraph.runs.empty())
      mDocument.paragraphs.push_back(mParagraph);
    return mDocument;
  }

private:
  ReaderState & state() { return mStack.back(); }

  void addChar(char16_t inChar)
  {
    if (mAttachmentChar && !state().skip) {
      mAttachmentChar = false;
      if (inChar == 0xAC || inChar == 0xFFFC)
        return;
    }
    if (mSkipChars > 0) {
      --mSkipChars;
      return;
    }
    if (state().skip)
      return;
    if (state().fontTable) {
      if (inChar == ';')
        endFont();
      else
        mFontName += (char)inChar;
      return;
    }
    mText += inChar;
  }

  void flushText()
  {
    if (mText.empty())
      return;
    TextStyle style = state().style;
    auto font = mFonts.find(state().font);
    style.family = font != mFonts.end() ? font->second : FontFamily::kSystem;
    auto name = mFontNames.find(state().font);
    if (name != mFontNames.end() && name->second != FontName(style.family))
      style.font = name->second;
    mParagraph.addText(UTF8FromUTF16(mText), style);
    mText.clear();
  }

  void endParagraph()
  {
    flushText();
    mDocument.paragraphs.push_back(mParagraph);
    ParagraphStyle style = mParagraph.style;
    mParagraph = RichParagraph();
    mParagraph.style = style;
  }

  void endFont()
  {
    std::string name = mFontName;
    while (!name.empty() && std::isspace((unsigned char)name.front()))
      name.erase(0, 1);
    mFonts[mFontNumber] = FamilyOfFont(name, mFontFamily);
    mFontNames[mFontNumber] = name;
    mFontName.clear();
    mFontFamily.clear();
  }

  void controlWord()
  {
    ++mAt;                        // the backslash
    if (mAt >= mIn.size())
      return;
    char c = mIn[mAt];
    if (!std::isalpha((unsigned char)c)) {
      ++mAt;
      bool wasStart = mDestinationStart;
      mDestinationStart = false;
      switch (c) {
        case '\\': case '{': case '}': addChar((char16_t)c); break;
        case '~': addChar(0xA0); break;
        case '_': addChar('-'); break;
        case '-': break;
        case '*': if (wasStart) state().skip = true; break;          // {\* an optional destination}
        case '\'':
          if (mAt + 1 < mIn.size()) {
            unsigned value = (unsigned)std::strtoul(mIn.substr(mAt, 2).c_str(), nullptr, 16);
            mAt += 2;
            addChar(CP1252((unsigned char)value));
          }
          break;
        case '\r': case '\n': endParagraph(); break;
        default: break;
      }
      return;
    }
    size_t start = mAt;
    while (mAt < mIn.size() && std::isalpha((unsigned char)mIn[mAt]))
      ++mAt;
    std::string word = mIn.substr(start, mAt - start);
    bool hasNumber = false;
    int number = 0;
    if (mAt < mIn.size() && (mIn[mAt] == '-' || std::isdigit((unsigned char)mIn[mAt]))) {
      size_t numberStart = mAt++;
      while (mAt < mIn.size() && std::isdigit((unsigned char)mIn[mAt]))
        ++mAt;
      number = std::atoi(mIn.substr(numberStart, mAt - numberStart).c_str());
      hasNumber = true;
    }
    if (mAt < mIn.size() && mIn[mAt] == ' ')
      ++mAt;
    bool wasStart = mDestinationStart;
    mDestinationStart = false;
    apply(word, hasNumber, number, wasStart);
  }

  void apply(const std::string & w, bool hasNumber, int n, bool inDestinationStart)
  {
    ReaderState & s = state();
    bool on = !hasNumber || n != 0;
    // destinations that aren't text
    static const char * const kSkip[] = { "colortbl", "stylesheet", "info", "pict", "header", "footer", "headerl", "headerr",
                                          "footerl", "footerr", "footnote", "object", "listtable", "listoverridetable",
                                          "revtbl", "rsidtbl", "generator", "xmlnstbl", "themedata", "colorschememapping",
                                          "latentstyles", "datastore", "fldinst", "pntext", "pntxta", "pntxtb", "nonshppict",
                                          "shp", "bkmkstart", "bkmkend", "field", "NeXTGraphic" };
    if (inDestinationStart || w == "pict")
      for (const char * skip : kSkip)
        if (w == skip) {
          s.skip = true;
          if (w == "NeXTGraphic")
            mAttachmentChar = true;       // (RTFD: 0xAC after its group)
          return;
        }
    if (w == "fonttbl") {
      flushText();
      s.fontTable = true;
      return;
    }
    if (s.fontTable) {
      if (w == "f" && hasNumber)
        mFontNumber = n;
      else if (w.size() > 1 && w[0] == 'f' && (w == "fnil" || w == "froman" || w == "fswiss" || w == "fmodern"
                                                 || w == "fscript" || w == "fdecor" || w == "ftech"))
        mFontFamily = w;
      return;
    }
    if (s.skip)
      return;
    if (w == "par" || w == "line" || w == "row" || w == "sect" || w == "page") {
      endParagraph();
      return;
    }
    flushText();
    if (w == "tab" || w == "cell") addChar('\t');
    else if (w == "u" && hasNumber) {
      addChar((char16_t)(n < 0 ? n + 65536 : n));
      mSkipChars = s.uc;
    } else if (w == "uc" && hasNumber) s.uc = n;
    else if (w == "emdash") addChar(0x2014);
    else if (w == "endash") addChar(0x2013);
    else if (w == "bullet") addChar(0x2022);
    else if (w == "lquote") addChar(0x2018);
    else if (w == "rquote") addChar(0x2019);
    else if (w == "ldblquote") addChar(0x201C);
    else if (w == "rdblquote") addChar(0x201D);
    else if (w == "plain") { s.style = TextStyle(); s.font = 0; }
    else if (w == "f" && hasNumber) s.font = n;
    else if (w == "fs" && hasNumber) s.style.size = std::max(1, n / 2);
    else if (w == "b") s.style.bold = on;
    else if (w == "i") s.style.italic = on;
    else if (w == "ul") s.style.underline = on;
    else if (w == "ulnone") s.style.underline = false;
    else if (w == "outl") s.style.outline = on;
    else if (w == "super") { s.style.superscript = on; s.style.subscript = false; }
    else if (w == "sub") { s.style.subscript = on; s.style.superscript = false; }
    else if (w == "nosupersub") { s.style.superscript = s.style.subscript = false; }
    else if (w == "pard") mParagraph.style = ParagraphStyle();
    else if (w == "ql") mParagraph.style.alignment = Alignment::kLeft;
    else if (w == "qc") mParagraph.style.alignment = Alignment::kCenter;
    else if (w == "qr") mParagraph.style.alignment = Alignment::kRight;
    else if (w == "qj") mParagraph.style.alignment = Alignment::kJustified;
    else if (w == "li" && hasNumber) mParagraph.style.leftIndent = n / 20;
    else if (w == "fi" && hasNumber) mParagraph.style.firstIndent = n / 20;
    else if (w == "ri" && hasNumber) mParagraph.style.rightIndent = n / 20;
    else if (w == "tqc") mTabKind = TabStop::kCenter;
    else if (w == "tqr") mTabKind = TabStop::kRight;
    else if (w == "tqdec") mTabKind = TabStop::kDecimal;
    else if (w == "tx" && hasNumber) {
      TabStop tab;
      tab.position = n / 20;
      tab.kind = mTabKind;
      mParagraph.style.tabs.push_back(tab);
      mTabKind = TabStop::kLeft;
    }
    else if (w == "margl" && hasNumber) mDocument.margins[0] = n / 20;
    else if (w == "margt" && hasNumber) mDocument.margins[1] = n / 20;
    else if (w == "margr" && hasNumber) mDocument.margins[2] = n / 20;
    else if (w == "margb" && hasNumber) mDocument.margins[3] = n / 20;
  }

  const std::string & mIn;
  size_t mAt = 0;
  std::vector<ReaderState> mStack;
  bool mDestinationStart = false;
  int mSkipChars = 0;
  bool mAttachmentChar = false;
  std::u16string mText;
  RichParagraph mParagraph;
  RichDocument mDocument;
  std::map<int, FontFamily> mFonts;
  std::map<int, std::string> mFontNames;
  int mFontNumber = 0;
  std::string mFontName, mFontFamily;
  TabStop::Kind mTabKind = TabStop::kLeft;
};

} // namespace


RichDocument ReadRTF(const std::string & inRTF)
{
  return RTFReader(inRTF).read();
}

} // namespace ncx
