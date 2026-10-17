/*
 File: Works.cc

 Newton Works documents (the Cocoa app's WorksToRTF, WorksFromText,
 WorksFromRTF):
   - 'paper: its text with its styles (fonts, and graphics in the text),
     rulers (alignment, indents, line spacing, tabs) and margins, as RTF
     with the graphics as PNGs (the Cocoa app: RTFD)
   - 'drawPaper: its shapes as a PNG (the Cocoa app: TIFF)
   - '|QFNewt:donv| (Don Vollum's QuickFigure spreadsheet): its cells as CSV
 and back: a text or RTF file as a 'paper (its text in saveData.text, a
 string: the internal store has no large objects for a txText yet,
 newton-framework B55; the Cocoa app did the same without them).

 A paper's text is saveData.txText (UTF-16, big-endian, in blocks of 512
 characters, txData saying how full each is) or saveData.text (a string).
 */

#include "translators/Translator.h"
#include "translators/NewtonText.h"
#include "translators/Render.h"
#include "translators/RichText.h"
#include "base/Dates.h"
#include "base/NewtonStrings.h"
#include "base/Strings.h"
#include "comms/DockRefs.h"
#include "platform/AppFolders.h"

#include "RSSymbols.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>

namespace ncx {

namespace {

uint32_t BigEndian16(const uint8_t * inBytes) { return (uint32_t)inBytes[0] << 8 | inBytes[1]; }
uint32_t BigEndian32(const uint8_t * inBytes)
{
  return (uint32_t)inBytes[0] << 24 | (uint32_t)inBytes[1] << 16 | (uint32_t)inBytes[2] << 8 | inBytes[3];
}

/** A paper's text: txText by txData (the Cocoa app's TXData: a type byte,
    the number of 512-character blocks, then (end, block) pairs for the
    blocks that aren't full, 0 at the end), or text. */
std::u16string PaperText(RefArg inSaveData)
{
  RefVar txText(GetFrameSlot(inSaveData, MakeSymbol("txText")));
  RefVar txData(GetFrameSlot(inSaveData, MakeSymbol("txData")));
  if (IsBinary(txText) && IsBinary(txData) && Length(txData) >= 3) {
    const uint8_t * data = (const uint8_t *)BinaryData(txData);
    size_t dataLength = Length(txData), at = 3;
    const uint8_t * chars = (const uint8_t *)BinaryData(txText);
    size_t numOfChars = Length(txText) / 2;
    size_t size = BigEndian16(data + 1) * 512;
    std::u16string text;
    size_t location = 0, length = 0;
    for (;;) {
      if (length != 0) {
        location = (location + length + 511) / 512 * 512;
        if (location >= size)
          break;
      }
      uint32_t end = at + 2 <= dataLength ? BigEndian16(data + at) : 0;
      at += 2;
      if (end == 0) {
        length = location >= size ? 0 : size - location;
      } else {
        uint32_t block = at + 2 <= dataLength ? BigEndian16(data + at) : 0;
        at += 2;
        length = block * 512 + end > location ? block * 512 + end - location : 0;
      }
      if (length == 0)
        break;
      for (size_t i = location; i < location + length && i < numOfChars; ++i)
        text += (char16_t)BigEndian16(chars + 2 * i);
    }
    // the last block may be zeros after the text
    while (!text.empty() && text.back() == 0)
      text.pop_back();
    return text;
  }
  return UTF16Of(RefVar(GetFrameSlot(inSaveData, SYMA(text))));
}

/** A ruler frame as a paragraph style. */
ParagraphStyle StyleOfRuler(RefArg inRuler)
{
  ParagraphStyle style;
  if (!IsFrame(inRuler))
    return style;
  RefVar justification(GetFrameSlot(inRuler, MakeSymbol("justification")));
  if (EQRef(justification, SYMA(right)))
    style.alignment = Alignment::kRight;
  else if (EQRef(justification, MakeSymbol("center")))
    style.alignment = Alignment::kCenter;
  else if (EQRef(justification, MakeSymbol("justified")))
    style.alignment = Alignment::kJustified;
  auto number = [&](const char * inSlot) {
    RefVar value(GetFrameSlot(inRuler, MakeSymbol(inSlot)));
    return ISINT(value) ? (int)RINT(value) : 0;
  };
  style.leftIndent = number("leftMargin");
  style.firstIndent = number("indent");
  style.rightIndent = number("rightMargin");
  int spacing = number("lineSpacing");
  style.lineSpacing = spacing > 0 ? spacing : 1;
  RefVar tabs(GetFrameSlot(inRuler, SYMA(tabs)));
  for (ArrayIndex i = 0, n = IsArray(tabs) ? Length(tabs) : 0; i < n; ++i) {
    RefVar tab(GetArraySlot(tabs, i));
    RefVar value(GetFrameSlot(tab, MakeSymbol("value")));
    if (!ISINT(value))
      continue;
    TabStop stop;
    stop.position = (int)RINT(value);
    RefVar kind(GetFrameSlot(tab, MakeSymbol("kind")));
    stop.kind = EQRef(kind, SYMA(right)) ? TabStop::kRight : EQRef(kind, MakeSymbol("center")) ? TabStop::kCenter
              : EQRef(kind, MakeSymbol("decimal")) ? TabStop::kDecimal : TabStop::kLeft;
    style.tabs.push_back(stop);
  }
  return style;
}

/** A 'paper as a document. */
RichDocument PaperDocument(RefArg inEntry)
{
  RichDocument document;
  RefVar saveData(GetFrameSlot(inEntry, MakeSymbol("saveData")));
  RefVar rulers(GetFrameSlot(saveData, MakeSymbol("rulers")));
  auto rulerAt = [&](size_t inAt) {
    size_t start = 0;
    for (ArrayIndex i = 0; IsArray(rulers) && i + 1 < Length(rulers); i += 2) {
      RefVar length(GetArraySlot(rulers, i));
      size_t end = start + (ISINT(length) ? (size_t)RINT(length) : 0);
      if (inAt < end || i + 2 >= Length(rulers))
        return StyleOfRuler(RefVar(GetArraySlot(rulers, i + 1)));
      start = end;
    }
    return ParagraphStyle();
  };
  AddStyledText(document, PaperText(saveData), RefVar(GetFrameSlot(saveData, SYMA(styles))), TextStyle(), rulerAt);
  RefVar margins(GetFrameSlot(inEntry, MakeSymbol("margins")));
  const char * const kSlots[4] = { "left", "top", "right", "bottom" };
  for (int i = 0; i < 4; ++i) {
    RefVar value(IsFrame(margins) ? GetFrameSlot(margins, MakeSymbol(kSlots[i])) : (Ref)NILREF);
    if (ISINT(value))
      document.margins[i] = (int)RINT(value);
  }
  return document;
}

/*-- QuickFigure -------------------------------------------------------------*/

const int kColumnsOnSheet = 40;

/** A cell's text: its formula ("=..."), or its value. */
std::string CellText(const uint8_t * inCells, size_t inCellsLength, uint32_t inOffset)
{
  if (inOffset == 0 || (size_t)inOffset + 8 > inCellsLength)
    return std::string();
  uint32_t length = BigEndian32(inCells + inOffset);
  if ((size_t)inOffset + 8 + length > inCellsLength)
    return std::string();
  std::vector<uint8_t> nsof(inCells + inOffset + 8, inCells + inOffset + 8 + length);
  RefVar cell(UnflattenBytes(nsof));
  RefVar formula(GetFrameSlot(cell, MakeSymbol("viewFormula")));
  if (IsString(formula))
    return "=" + UTF8FromString(formula);
  RefVar value(GetFrameSlot(cell, MakeSymbol("value")));
  if (IsString(value))
    return UTF8FromStringLF(value);
  if (ISINT(value))
    return std::to_string(RINT(value));
  if (IsReal(value)) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.15g", CDouble(value));
    return text;
  }
  return std::string();
}

std::string CSVField(const std::string & inText)
{
  if (inText.find_first_of(",\"\n\r") == std::string::npos)
    return inText;
  return "\"" + ReplaceAll(inText, "\"", "\"\"") + "\"";
}

/** A QuickFigure sheet as CSV: 40 columns a row, cellAddresses (an offset
    a cell, 0: empty) into cells (a length, 4 bytes, the cell's NSOF), up
    to lastCellIndex; as many rows and columns as its cells use. */
std::string SpreadsheetCSV(RefArg inEntry)
{
  RefVar cells(GetFrameSlot(inEntry, MakeSymbol("cells")));
  RefVar addresses(GetFrameSlot(inEntry, MakeSymbol("cellAddresses")));
  if (!IsBinary(cells) || !IsBinary(addresses))
    return std::string();
  size_t count = Length(addresses) / 4;
  RefVar last(GetFrameSlot(inEntry, MakeSymbol("lastCellIndex")));
  if (ISINT(last))
    count = std::min(count, (size_t)RINT(last) / 4 + 1);
  const uint8_t * cellData = (const uint8_t *)BinaryData(cells);
  const uint8_t * addressData = (const uint8_t *)BinaryData(addresses);
  std::vector<std::vector<std::string>> rows;
  size_t columns = 0;
  for (size_t index = 0; index < count; ++index) {
    std::string text = CellText(cellData, Length(cells), BigEndian32(addressData + index * 4));
    if (text.empty())
      continue;
    size_t row = index / kColumnsOnSheet, column = index % kColumnsOnSheet;
    if (rows.size() <= row)
      rows.resize(row + 1);
    if (rows[row].size() <= column)
      rows[row].resize(column + 1);
    rows[row][column] = text;
    columns = std::max(columns, column + 1);
  }
  std::string csv;
  for (const std::vector<std::string> & row : rows) {
    for (size_t column = 0; column < columns; ++column)
      csv += (column ? "," : "") + (column < row.size() ? CSVField(row[column]) : std::string());
    csv += "\n";
  }
  return csv;
}


class WorksExporter : public Exporter
{
public:
  std::filesystem::path exportEntry(RefArg inEntry) override
  {
    RefVar cls(GetFrameSlot(inEntry, SYMA(class)));
    std::string name = EntryFileName(inEntry);
    if (EQRef(cls, MakeSymbol("paper")))
      return write(WriteRTF(PaperDocument(inEntry)), name, "rtf");
    if (EQRef(cls, MakeSymbol("drawPaper"))) {
      RefVar saveData(GetFrameSlot(inEntry, MakeSymbol("saveData")));
      std::string png = PNGData(RenderShapes(RefVar(GetFrameSlot(saveData, MakeSymbol("shapes")))));
      return png.empty() ? std::filesystem::path() : write(png, name, "png");
    }
    if (EQRef(cls, MakeSymbol("QFNewt:donv"))) {
      std::string csv = SpreadsheetCSV(inEntry);
      return csv.empty() ? std::filesystem::path() : write(csv, name, "csv");
    }
    return std::filesystem::path();
  }
};


/*-- import ------------------------------------------------------------------*/

/** A 'paper of a document (the Cocoa app's WorksFromText: the text in
    saveData.text, its styles, one ruler for all of it). */
Ref PaperEntry(const RichDocument & inDocument, const std::string & inTitle)
{
  std::u16string text;
  RefVar styles;
  NewtonStyledText(inDocument.paragraphs, text, styles);
  RefVar string(MakeStringFromUTF8(UTF8FromUTF16(text)));
  RefVar rulers(MakeArray(2));
  SetArraySlot(rulers, 0, MAKEINT((int64_t)text.size()));
  RefVar ruler(AllocateFrame());
  ParagraphStyle style = inDocument.paragraphs.empty() ? ParagraphStyle() : inDocument.paragraphs[0].style;
  static const char * const kJustification[] = { "left", "center", "right", "justified" };
  SetFrameSlot(ruler, MakeSymbol("justification"), MakeSymbol(kJustification[(int)style.alignment]));
  SetFrameSlot(ruler, MakeSymbol("lineSpacing"), MAKEINT((int)(style.lineSpacing + 0.5)));
  SetFrameSlot(ruler, MakeSymbol("indent"), MAKEINT(style.firstIndent));
  SetFrameSlot(ruler, MakeSymbol("leftMargin"), MAKEINT(style.leftIndent));
  SetFrameSlot(ruler, MakeSymbol("rightMargin"), MAKEINT(style.rightIndent));
  SetFrameSlot(ruler, SYMA(tabs), RA(NILREF));
  SetArraySlot(rulers, 1, ruler);

  RefVar saveData(AllocateFrame());
  SetFrameSlot(saveData, SYMA(text), string);
  SetFrameSlot(saveData, SYMA(styles), styles);
  SetFrameSlot(saveData, MakeSymbol("rulers"), rulers);
  RefVar entry(AllocateFrame());
  SetFrameSlot(entry, SYMA(class), MakeSymbol("paper"));
  SetFrameSlot(entry, MakeSymbol("saveData"), saveData);
  SetFrameSlot(entry, SYMA(title), RefVar(MakeStringFromUTF8(inTitle)));
  std::string summary = ReplaceControlCharacters(UTF8FromUTF16(text.substr(0, 100)), " ");
  SetFrameSlot(entry, MakeSymbol("summary"), RefVar(MakeStringFromUTF8(summary)));
  if (inDocument.margins[0] >= 0) {
    RefVar margins(AllocateFrame());
    const char * const kSlots[4] = { "left", "top", "right", "bottom" };
    for (int i = 0; i < 4; ++i)
      SetFrameSlot(margins, MakeSymbol(kSlots[i]), MAKEINT(inDocument.margins[i] >= 0 ? inDocument.margins[i] : 72));
    SetFrameSlot(entry, MakeSymbol("margins"), margins);
  }
  SetFrameSlot(entry, SYMA(version), MAKEINT(1));
  SetFrameSlot(entry, SYMA(timestamp), MAKEINT(NewtonFromTime(Now())));
  return entry;
}


class WorksImporter : public Importer
{
public:
  explicit WorksImporter(bool inRTF) : mRTF(inRTF) { }

  bool begin(const std::filesystem::path & inFile, const TranslatorContext & inContext) override
  {
    Importer::begin(inFile, inContext);
    std::ifstream in(inFile, std::ios::binary);
    if (!in) {
      mWhy = "can't read the file";
      return false;
    }
    mData.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
  }

  Ref next(std::string &) override
  {
    if (mDone)
      return NILREF;
    mDone = true;
    RichDocument document;
    if (mRTF) {
      document = ReadRTF(mData);
    } else {
      // plain text in Geneva 9 (the Cocoa app's), a paragraph a line
      TextStyle style;
      style.family = FontFamily::kSimple;
      style.size = 9;
      std::string text = DesktopLineEnds(mData);
      size_t start = 0;
      for (;;) {
        size_t end = text.find('\n', start);
        RichParagraph para;
        para.addText(text.substr(start, end == std::string::npos ? std::string::npos : end - start), style);
        document.paragraphs.push_back(para);
        if (end == std::string::npos)
          break;
        start = end + 1;
      }
    }
    return PaperEntry(document, UTF8FromPath(mFile.stem()));
  }

private:
  bool mRTF;
  bool mDone = false;
  std::string mData;
};

} // namespace


std::unique_ptr<Exporter> MakeWorksExporter() { return std::make_unique<WorksExporter>(); }
std::unique_ptr<Importer> MakeWorksFromText() { return std::make_unique<WorksImporter>(false); }
std::unique_ptr<Importer> MakeWorksFromRTF() { return std::make_unique<WorksImporter>(true); }

} // namespace ncx
