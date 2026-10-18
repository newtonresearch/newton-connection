/*
 File: EntryPreview.h

 An entry as the inspector shows it (PORTING.md 7.5; the Cocoa app's
 inspector exported the entry and showed the file with QuickLook): the
 entry exported by its translator into a folder of its own, then as a
 document. Rich text (notes, Works) comes as the translator made it, with
 its pictures; a picture (a drawing, an image) as a picture; text files
 (vCard, iCalendar, CSV, NewtonScript) as text; others named. Newton
 objects: run as object work. No FLTK and no framework headers here.
 */

#ifndef NCX_UI_ENTRYPREVIEW_H
#define NCX_UI_ENTRYPREVIEW_H

#include "translators/RichText.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace ncx {

struct EntryPreview
{
  RichDocument document;
  bool monospaced = false;        // a text file: shown as it is
  std::string format;             // "vCard", "RTF", ...
  std::string error;
};

/** The entry of the app (its name: the translator's context) as the
    inspector shows it; inFolder: where the export may write (emptied). */
EntryPreview ReadEntryPreview(const std::filesystem::path & inFile, int64_t inEntryPk, const std::string & inAppName,
                              const std::filesystem::path & inFolder);

} // namespace ncx

#endif // NCX_UI_ENTRYPREVIEW_H
