/*
 File: Translator.h

 Translating soup entries to desktop files and back (PORTING.md 6.1; the
 Cocoa app's NCXTranslator and NCXPlugInController). An Exporter turns
 entries into files, one each or all of them in one (aggregating, as Names
 into one .vcf); an Importer turns a file into entries, one at a time.

 The translators are built in, in one table (Formats.cc). The Cocoa app
 loaded plug-in bundles and read their tables from their Info.plists; the
 tables are the same here:
   - export by the entry's class ('person -> vCard, 'meeting ->
     iCalendar, ...), or as the Cocoa app guessed it for an entry without
     one (a package, a Names card, an image, text); anything else is
     printed as NewtonScript (the default translator)
   - import by the Newton app and the file's extension (Names: .vcf, Dates:
     .ics, Notes: .txt, ...), into the soup the format names (a translator
     may choose another for an entry: Dates' repeating meetings)

 Only the dock thread (or a thread with the object system: CallNewton)
 runs them.
 */

#ifndef NCX_TRANSLATORS_TRANSLATOR_H
#define NCX_TRANSLATORS_TRANSLATOR_H

#include "Objects.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ncx {

/** How notes and Works papers are written: RTF with the pictures in it
    (decision D4), RTFD (a folder: TXT.rtf and the pictures as files, which
    Apple's TextEdit and Pages show), or RTFD only if there are pictures
    (the Cocoa app's way: the default on macOS). */
enum class RichTextFormat { kRTF, kRTFD, kRTFDWithPictures };
RichTextFormat DefaultRichTextFormat();

struct TranslatorContext
{
  std::filesystem::path folder;     // export: where the files go
  std::string appName;              // the Newton app ("Names")
  int64_t userFont = 0;             // the Newton's user font (a packed font spec), 0: none
  RichTextFormat richText = DefaultRichTextFormat();
};

struct RichDocument;

class Exporter
{
public:
  virtual ~Exporter() = default;
  virtual void begin(const TranslatorContext & inContext) { mContext = inContext; }
  /** The file written for the entry, or empty (nothing, or written in
      end()). */
  virtual std::filesystem::path exportEntry(RefArg inEntry) = 0;
  /** An aggregating translator's file. */
  virtual std::filesystem::path end() { return std::filesystem::path(); }

protected:
  /** inData into the folder as "<inName>.<inExtension>": the name without
      characters no file system takes, "untitled" if it is empty, and
      "<inName>-2" ... if the file exists already (nothing is overwritten).
      The file, or empty if it can't be written. */
  std::filesystem::path write(const std::string & inData, const std::string & inName, const std::string & inExtension);
  /** A document as RTF or RTFD (the context's richText), named as write. */
  std::filesystem::path writeRichText(const RichDocument & inDocument, const std::string & inName);
  /** "<inName>.<inExtension>" in the folder, a name no file has yet (as
      write); empty if there is none. */
  std::filesystem::path freePath(const std::string & inName, const std::string & inExtension);

  TranslatorContext mContext;
};

class Importer
{
public:
  virtual ~Importer() = default;
  /** False (and why()) if the file can't be read. */
  virtual bool begin(const std::filesystem::path & inFile, const TranslatorContext & inContext);
  /** The next entry, or NILREF at the end. ioSoup: the format's soup; the
      translator may set another for this entry. */
  virtual Ref next(std::string & ioSoup) = 0;
  virtual void end() { }
  const std::string & why() const { return mWhy; }

protected:
  std::filesystem::path mFile;
  TranslatorContext mContext;
  std::string mWhy;
};

struct ExportFormat
{
  std::string translator;                     // which: classes of one translator share it in a session
  std::string entryClass;                     // lower case: "person", "*package*", ...
  std::string extension;                      // of the files: "vcf"
  std::string description;                    // "vCard"
  std::function<std::unique_ptr<Exporter>()> make;
};

struct ImportFormat
{
  std::string app;                            // the Newton app: "Names"
  std::vector<std::string> extensions;        // lower case, no dot: "vcf"
  std::string soup;                           // where its entries go
  std::string description;
  std::function<std::unique_ptr<Importer>()> make;
};

const std::vector<ExportFormat> & ExportFormats();
const std::vector<ImportFormat> & ImportFormats();

/** The key an entry is exported by: its class in lower case; without one
    as the Cocoa app guessed it: "*package*" (a pkgRef), "company" (a
    company slot), "person" (a name of class 'person), "*image*" (an image
    frame), "text" (a text slot); "" if nothing fits. */
std::string ExportClassOf(RefArg inEntry);

/** The translator for an entry's class (ExportClassOf); nullptr: the
    default translator (NewtonScript printed). */
const ExportFormat * FindExportFormat(const std::string & inEntryClass);

/** The Newton apps that import a file of that extension (sorted). */
std::vector<std::string> ImportAppsFor(const std::filesystem::path & inFile);
/** The format an app imports the file with, nullptr if none. */
const ImportFormat * FindImportFormat(const std::string & inApp, const std::filesystem::path & inFile);

/** Exporting the entries of an app: each by its translator, each
    translator begun once and ended at end() (NCXPlugInController's
    beginExport, export:, endExport). */
class ExportSession
{
public:
  explicit ExportSession(const TranslatorContext & inContext) : mContext(inContext) { }
  void add(RefArg inEntry);
  /** All files written, in order. */
  std::vector<std::filesystem::path> end();

private:
  Exporter & exporterFor(RefArg inEntry);

  TranslatorContext mContext;
  std::vector<std::unique_ptr<Exporter>> mExporters;   // in the order they were begun
  std::map<std::string, Exporter *> mByTranslator;      // by ExportFormat::translator ("" the default)
  std::vector<std::filesystem::path> mFiles;
};

/*-- helpers for the translators ---------------------------------------------*/

/** A bounds frame {left, top, right, bottom}. */
Ref MakeBounds(int inLeft, int inTop, int inRight, int inBottom);
/** A paragraph of a note: {viewStationery: 'para, viewBounds, text} (the
    ROM's template, magic pointer 267), and the user font if there is one. */
Ref MakeParagraph(const std::string & inText, int inLeft, int inTop, int inRight, int inBottom, int64_t inUserFont);
/** The text of the paragraphs ('para) of a note's data, a line each (LF). */
std::string ParagraphsText(RefArg inData);
/** A string slot as UTF-8 with LF line ends ("" if it isn't one). */
std::string StringSlot(RefArg inFrame, const char * inSlot);
/** A name frame's "first last". */
std::string FullName(RefArg inName);
/** A file name for an entry (the Cocoa app's makeFilename): its title,
    else the date of its timestamp ("Oct 18, 2026"), else "untitled". */
std::string EntryFileName(RefArg inEntry);

} // namespace ncx

#endif // NCX_TRANSLATORS_TRANSLATOR_H
