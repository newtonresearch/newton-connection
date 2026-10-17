/*
 File: NewtonText.cc

 The Newton's styled text and RichText (NewtonText.h).
 */

#include "translators/NewtonText.h"
#include "translators/Render.h"
#include "base/NewtonStrings.h"
#include "base/Strings.h"

#include "RSSymbols.h"

#include <algorithm>
#include <cctype>

namespace ncx {

namespace {

// a packed font spec: family bits 0-9, size 10-19, face 20-29
const int64_t kFamilyMask = 0x3FF, kSizeShift = 10, kSizeMask = 0x3FF, kFaceShift = 20;
enum { kBold = 1, kItalic = 2, kUnderline = 4, kOutline = 8, kSuperScript = 128, kSubScript = 256 };

void SetFace(TextStyle & ioStyle, int64_t inFace)
{
  ioStyle.bold = inFace & kBold;
  ioStyle.italic = inFace & kItalic;
  ioStyle.underline = inFace & kUnderline;
  ioStyle.outline = inFace & kOutline;
  ioStyle.superscript = inFace & kSuperScript;
  ioStyle.subscript = inFace & kSubScript;
}

std::string Lower(std::string inText)
{
  for (char & c : inText)
    c = (char)std::tolower((unsigned char)c);
  return inText;
}

/** A font frame's family symbol ('espy, 'newYork, 'geneva, 'handwriting;
    Works' 'schoolBookFont, 'CourierFont) on a style. */
void SetFamily(TextStyle & ioStyle, const std::string & inSymbol)
{
  std::string name = Lower(inSymbol);
  ioStyle.font.clear();
  if (name.find("espy") != std::string::npos || name.find("system") != std::string::npos)
    ioStyle.family = FontFamily::kSystem;
  else if (name.find("newyork") != std::string::npos || name.find("fancy") != std::string::npos)
    ioStyle.family = FontFamily::kFancy;
  else if (name.find("geneva") != std::string::npos || name.find("simple") != std::string::npos)
    ioStyle.family = FontFamily::kSimple;
  else if (name.find("hand") != std::string::npos || name.find("casual") != std::string::npos)
    ioStyle.family = FontFamily::kHandwriting;
  else if (name.find("courier") != std::string::npos) {
    ioStyle.family = FontFamily::kSimple;
    ioStyle.font = "Courier";
  } else if (name.find("school") != std::string::npos) {
    ioStyle.family = FontFamily::kFancy;
    ioStyle.font = "Century Schoolbook";
  } else {
    ioStyle.family = FontFamily::kSystem;
  }
}

} // namespace


TextStyle StyleOfFontSpec(RefArg inSpec, const TextStyle & inDefault)
{
  TextStyle style = inDefault;
  if (ISINT(inSpec)) {
    int64_t spec = RINT(inSpec);
    int64_t family = spec & kFamilyMask;
    style.family = family <= 3 ? (FontFamily)family : FontFamily::kSystem;
    style.font.clear();
    int64_t size = (spec >> kSizeShift) & kSizeMask;
    if (size > 0)
      style.size = (int)size;
    SetFace(style, spec >> kFaceShift);
  } else if (IsFrame(inSpec)) {
    RefVar family(GetFrameSlot(inSpec, SYMA(family)));
    if (IsSymbol(family))
      SetFamily(style, UTF8FromSymbol(family));
    RefVar size(GetFrameSlot(inSpec, SYMA(size)));
    if (ISINT(size) && RINT(size) > 0)
      style.size = (int)RINT(size);
    RefVar face(GetFrameSlot(inSpec, SYMA(face)));
    SetFace(style, ISINT(face) ? RINT(face) : 0);
  }
  return style;
}


int64_t FontSpecOfStyle(const TextStyle & inStyle)
{
  int64_t face = (inStyle.bold ? kBold : 0) | (inStyle.italic ? kItalic : 0) | (inStyle.underline ? kUnderline : 0)
                 | (inStyle.outline ? kOutline : 0) | (inStyle.superscript ? kSuperScript : 0) | (inStyle.subscript ? kSubScript : 0);
  int64_t size = std::min(std::max(inStyle.size, 1), 255);
  return (int64_t)inStyle.family | size << kSizeShift | face << kFaceShift;
}


std::u16string UTF16Of(RefArg inString)
{
  return IsString(inString) ? UTF16FromUTF8(UTF8FromString(inString)) : std::u16string();
}


void AddStyledText(RichDocument & ioDocument, const std::u16string & inText, RefArg inStyles, const TextStyle & inDefault,
                   const std::function<ParagraphStyle(size_t)> & inParagraphStyle, bool inNewParagraph)
{
  auto startParagraph = [&](size_t inAt) {
    RichParagraph para;
    if (inParagraphStyle)
      para.style = inParagraphStyle(inAt);
    ioDocument.paragraphs.push_back(para);
  };
  if (inNewParagraph || ioDocument.paragraphs.empty())
    startParagraph(0);
  // the runs: [length, style, ...]; what they don't cover is in the default
  struct Run { size_t start, length; RefStruct style; };
  std::vector<Run> runs;
  size_t at = 0;
  for (ArrayIndex i = 0; IsArray(inStyles) && i + 1 < Length(inStyles); i += 2) {
    RefVar length(GetArraySlot(inStyles, i));
    if (!ISINT(length) || RINT(length) <= 0)
      continue;
    runs.push_back({ at, (size_t)RINT(length), RefStruct(GetArraySlot(inStyles, i + 1)) });
    at += (size_t)RINT(length);
  }
  if (at < inText.size())
    runs.push_back({ at, inText.size() - at, RefStruct(NILREF) });

  for (const Run & run : runs) {
    if (run.start >= inText.size())
      break;
    size_t length = std::min(run.length, inText.size() - run.start);
    RefVar style(run.style);
    // a picture in the text: an ink word, a graphic
    Pixels picture;
    if (IsBinary(style) && EQRef(ClassOf(style), SYMA(inkWord)))
      picture = RenderInkWord(style);
    else if (IsFrame(style) && EQRef(ClassOf(style), MakeSymbol("graphics"))) {
      RefVar shape(GetFrameSlot(style, MakeSymbol("shape")));
      picture = RenderBitmap(shape);
      if (picture.empty())
        picture = RenderShapes(shape);
    }
    if (!picture.empty() || (IsBinary(style) && EQRef(ClassOf(style), SYMA(inkWord)))) {
      ioDocument.paragraphs.back().addPicture(PNGData(picture), picture.width, picture.height);
      continue;
    }
    TextStyle textStyle = StyleOfFontSpec(style, inDefault);
    std::u16string part;
    for (size_t i = run.start; i < run.start + length; ++i) {
      char16_t c = inText[i];
      if (c == u'\r' || c == u'\n') {
        if (c == u'\r' && i + 1 < inText.size() && inText[i + 1] == u'\n')
          ++i;
        ioDocument.paragraphs.back().addText(UTF8FromUTF16(part), textStyle);
        part.clear();
        startParagraph(i + 1);
      } else if (c == 0xF701) {
        // an ink word's place without its ink: nothing
      } else {
        part += c;
      }
    }
    ioDocument.paragraphs.back().addText(UTF8FromUTF16(part), textStyle);
  }
  // a line end at the end ends its paragraph: no empty one after it
  if (!inText.empty() && (inText.back() == u'\r' || inText.back() == u'\n') && ioDocument.paragraphs.size() > 1
      && ioDocument.paragraphs.back().runs.empty())
    ioDocument.paragraphs.pop_back();
}


void NewtonStyledText(const std::vector<RichParagraph> & inParagraphs, std::u16string & outText, RefVar & outStyles)
{
  outText.clear();
  outStyles = MakeArray(0);
  int64_t lastSpec = -1;
  size_t lastLength = 0;
  auto add = [&](const std::u16string & inText, int64_t inSpec) {
    if (inText.empty())
      return;
    outText += inText;
    if (inSpec == lastSpec) {
      lastLength += inText.size();
      SetArraySlot(outStyles, Length(outStyles) - 2, MAKEINT((int64_t)lastLength));
      return;
    }
    AddArraySlot(outStyles, MAKEINT((int64_t)inText.size()));
    AddArraySlot(outStyles, MAKEINT(inSpec));
    lastSpec = inSpec;
    lastLength = inText.size();
  };
  for (size_t p = 0; p < inParagraphs.size(); ++p) {
    TextStyle style;
    for (const TextRun & run : inParagraphs[p].runs) {
      if (!run.png.empty())
        continue;                 // (pictures aren't Newton text)
      style = run.style;
      add(UTF16FromUTF8(run.text), FontSpecOfStyle(run.style));
    }
    if (p + 1 < inParagraphs.size())
      add(u"\r", lastSpec >= 0 ? lastSpec : FontSpecOfStyle(style));
  }
}

} // namespace ncx
