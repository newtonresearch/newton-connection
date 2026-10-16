/*
 File: AppFolders.h

 Where NCX keeps its files (PORTING.md 2.3), per system:

   app data   macOS    ~/Library/Application Support/Newton Connection
                       (where the Cocoa app kept them: they are found)
              Windows  %APPDATA%\Newton Connection
              Linux    $XDG_DATA_HOME/newton-connection
                       (~/.local/share/newton-connection)
   log file   macOS    ~/Library/Logs/NewtonConnection.log (as before)
              Windows  %LOCALAPPDATA%\Newton Connection\NewtonConnection.log
              Linux    $XDG_STATE_HOME/newton-connection/NewtonConnection.log
                       (~/.local/state/...)

 NCX_DATA_DIR and NCX_LOG_FILE override them (tests, a portable copy).
 Paths are std::filesystem::path; names and paths as text are UTF-8.
 */

#ifndef NCX_PLATFORM_APPFOLDERS_H
#define NCX_PLATFORM_APPFOLDERS_H

#include <filesystem>
#include <string>

namespace ncx {

/** The app data folder, made if it isn't there. Empty if there is no home
    folder to put it in. */
std::filesystem::path AppDataFolder();

/** A file in it (inName: UTF-8). */
std::filesystem::path AppDataFile(const std::string & inName);

/** The log file; its folder is made if it isn't there. */
std::filesystem::path LogFilePath();

/** The user's home folder (HOME, USERPROFILE on Windows); "." if unknown. */
std::filesystem::path HomeFolder();

/** This computer's name (UTF-8), as the Newton's file browser shows it. */
std::string ComputerName();

/** Paths and UTF-8 (on Windows paths are UTF-16 underneath). */
std::filesystem::path PathFromUTF8(const std::string & inText);
std::string UTF8FromPath(const std::filesystem::path & inPath);

} // namespace ncx

#endif // NCX_PLATFORM_APPFOLDERS_H
