/*
 File: Edits.h

 What the UI does to a backup file without a Newton (PORTING.md 7.4): delete
 entries from it, export entries from it. Newton objects (and the file
 written while the dock may write others): run as object work
 (App::objectWork). Each returns what came of it, for the status line. No
 FLTK and no framework headers here.
 */

#ifndef NCX_UI_EDITS_H
#define NCX_UI_EDITS_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ncx {

/** The entries leave the file (the next backup deletes them on the Newton,
    asking if it isn't clear). "Deleted 3 entries" or why not; outOK. */
std::string DeleteFromFile(const std::filesystem::path & inFile, const std::vector<int64_t> & inEntryPks, bool & outOK);

/** The app's entries on the store (inEntryPks: those, none: all of them)
    into files in inFolder, as its translator writes them. "Exported 3
    entries into 2 files" or why not; outOK. */
std::string ExportEntries(const std::filesystem::path & inFile, int64_t inAppPk, int64_t inStorePk,
                          const std::filesystem::path & inFolder, const std::vector<int64_t> & inEntryPks, bool & outOK);

} // namespace ncx

#endif // NCX_UI_EDITS_H
