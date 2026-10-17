/*
 File: Transfer.h

 Exporting an app's entries from a .newtondevice file to desktop files, and
 importing desktop files into it (the Cocoa app's export of a selection,
 and SoupViewController's import:). Imported entries are made on the
 desktop (AddImportedEntry): the next backup, or SoupEditor::add with a
 Newton docked, sends them to the Newton.

 With the object system (the dock thread, or CallNewton).
 */

#ifndef NCX_TRANSLATORS_TRANSFER_H
#define NCX_TRANSLATORS_TRANSFER_H

#include "model/Records.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ncx {

class DeviceFile;

struct ExportResult
{
  size_t entries = 0;
  std::vector<std::filesystem::path> files;
};

/** The entries of the app's soups (on inStorePk, or every store) into files
    in inFolder. inEntryPks: only those (empty: all). */
ExportResult ExportApp(DeviceFile & inFile, const AppRecord & inApp, int64_t inStorePk, const std::filesystem::path & inFolder,
                       const std::vector<int64_t> & inEntryPks = {});

struct ImportResult
{
  size_t entries = 0;                         // made
  std::map<std::string, size_t> soups;        // made, by soup
  size_t skipped = 0;                         // their soup isn't on the store
  std::vector<int64_t> entryPks;              // the entries made
  std::string error;                          // the file couldn't be imported at all
};

/** inSource imported into inApp's soups on inStore (the format the app
    imports the file's kind with, ImportAppsFor). */
ImportResult ImportFile(DeviceFile & ioFile, const StoreRecord & inStore, const std::string & inApp,
                        const std::filesystem::path & inSource);

} // namespace ncx

#endif // NCX_TRANSLATORS_TRANSFER_H
