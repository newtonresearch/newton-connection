/*
 File: RichText.h

 Styled text with pictures, and RTF (PORTING.md 6.4, decision D4: RTF
 with embedded PNGs, instead of the Cocoa app's RTFD bundles). Notes and
 Works become a RichDocument (NewtonText.h) and are written as RTF; an RTF
 file read back becomes a RichDocument (its pictures left out).

 Sizes are points (the Newton's pixels are about that); RTF counts twips,
 20 a point. Fonts are the Newton's four families: their RTF names are
 those the Cocoa app used, with RTF's family hint for a word processor
 that doesn't have them.

 No Newton objects here.
 */

#ifndef NCX_TRANSLATORS_RICHTEXT_H
#define NCX_TRANSLATORS_RICHTEXT_H

#include <string>
#include <vector>

namespace ncx {

enum class FontFamily { kSystem, kFancy, kSimple, kHandwriting };

struct TextStyle
{
  FontFamily family = FontFamily::kSystem;
  std::string font;               // a font of its own (Works' Courier, Schoolbook), else the family's
  int size = 12;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool outline = false;
  bool superscript = false;
  bool subscript = false;

  bool operator==(const TextStyle & inOther) const;
  bool operator!=(const TextStyle & inOther) const { return !(*this == inOther); }
};

/** Text in one style, or a picture (png not empty). */
struct TextRun
{
  std::string text;               // UTF-8, no line ends (they end paragraphs)
  TextStyle style;
  std::string png;                // a PNG file's bytes
  int width = 0;                  // the picture's size, points
  int height = 0;
};

enum class Alignment { kLeft, kCenter, kRight, kJustified };

struct TabStop
{
  int position = 0;               // points from the left margin
  enum Kind { kLeft, kCenter, kRight, kDecimal } kind = kLeft;
};

struct ParagraphStyle
{
  Alignment alignment = Alignment::kLeft;
  int firstIndent = 0;            // points, from leftIndent
  int leftIndent = 0;
  int rightIndent = 0;
  double lineSpacing = 1.0;       // 1 single, 2 double
  std::vector<TabStop> tabs;

  bool operator==(const ParagraphStyle & inOther) const;
};

struct RichParagraph
{
  std::vector<TextRun> runs;
  ParagraphStyle style;

  /** A run of text, joined to the last one if it has the same style. */
  void addText(const std::string & inText, const TextStyle & inStyle);
  void addPicture(const std::string & inPNG, int inWidth, int inHeight);
  std::string text() const;       // its text (pictures left out)
};

struct RichDocument
{
  std::vector<RichParagraph> paragraphs;
  int margins[4] = { -1, -1, -1, -1 };     // left, top, right, bottom in points; -1: not set

  std::string text() const;       // the paragraphs' text, a LF after each but the last
};

/** A picture of an RTFD document: its file's name in the bundle, its PNG. */
struct Attachment
{
  std::string name;
  std::string png;
};

/** The document as RTF (ANSI with \uN for the rest, a font table of the
    families used, pictures as \pngblip). outAttachments: an RTFD
    document's TXT.rtf instead, its pictures as files of the bundle (as
    Apple writes them: \NeXTGraphic, which TextEdit and Pages show and
    \pngblip they don't). */
std::string WriteRTF(const RichDocument & inDocument, std::vector<Attachment> * outAttachments = nullptr);

/** A document has pictures. */
bool HasPictures(const RichDocument & inDocument);

/** An RTF text as a document: paragraphs, fonts by family, size, bold,
    italic, underline, super- and subscript, alignment, indents, tabs.
    Pictures, tables (as tabs), headers and the like are left out. */
RichDocument ReadRTF(const std::string & inRTF);

/** A font's name and RTF family for a Newton family, and back from a name
    (and an RTF family: "fswiss", "froman", ...). */
const char * FontName(FontFamily inFamily);
FontFamily FamilyOfFont(const std::string & inName, const std::string & inRTFFamily = std::string());

} // namespace ncx

#endif // NCX_TRANSLATORS_RICHTEXT_H
