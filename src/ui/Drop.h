/*
 File: Drop.h

 Files dropped on the app's windows (PORTING.md 7.4, 7.8). FLTK hands a
 drop over as text (FL_PASTE, Fl::event_text()): one file a line, as a
 path, or as a file: URL (Linux file managers, %-escaped). Entries dragged
 between the app's windows go as text too, in a form of NCX's own.
 */

#ifndef NCX_UI_DROP_H
#define NCX_UI_DROP_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ncx {

/** The files a drop names (lines that aren't files are left out). */
std::vector<std::filesystem::path> DroppedFiles(const std::string & inText);

/** Entries dragged from one of NCX's windows to another (D9), as the text
    FLTK's drag and drop carries: "ncx-entries:<count>", the backup file,
    the entries' pks. */
std::string EntriesPayload(const std::filesystem::path & inFile, const std::vector<int64_t> & inEntryPks);
/** False if the text isn't such a payload. */
bool ParseEntriesPayload(const std::string & inText, std::filesystem::path & outFile, std::vector<int64_t> & outEntryPks);

} // namespace ncx

#endif // NCX_UI_DROP_H
