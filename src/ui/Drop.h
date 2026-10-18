/*
 File: Drop.h

 Files dropped on the app's windows (PORTING.md 7.4, 7.8). FLTK hands a
 drop over as text (FL_PASTE, Fl::event_text()): one file a line, as a
 path, or as a file: URL (Linux file managers, %-escaped).
 */

#ifndef NCX_UI_DROP_H
#define NCX_UI_DROP_H

#include <filesystem>
#include <string>
#include <vector>

namespace ncx {

/** The files a drop names (lines that aren't files are left out). */
std::vector<std::filesystem::path> DroppedFiles(const std::string & inText);

} // namespace ncx

#endif // NCX_UI_DROP_H
