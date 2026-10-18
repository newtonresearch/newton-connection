/*
 File: Edits.cc

 What the UI does to a backup file without a Newton (Edits.h).
 */

#include "ui/Edits.h"

#include "base/NewtonCall.h"
#include "model/DeviceFile.h"
#include "platform/AppFolders.h"
#include "translators/Transfer.h"

#include <exception>
#include <memory>

namespace ncx {

namespace {

std::string Count(size_t inCount, const char * inOne, const char * inMore)
{
  return std::to_string(inCount) + " " + (inCount == 1 ? inOne : inMore);
}

} // namespace


std::string DeleteFromFile(const std::filesystem::path & inFile, const std::vector<int64_t> & inEntryPks, bool & outOK)
{
  outOK = false;
  std::string error;
  std::unique_ptr<DeviceFile> file = DeviceFile::Open(inFile, DeviceFile::kReadWrite, error);
  if (!file)
    return "Not deleted: " + error;
  try {
    DeviceFile::Transaction transaction(*file);
    for (int64_t pk : inEntryPks)
      file->deleteEntry(pk);
    transaction.commit();
  } catch (const std::exception & e) {
    return std::string("Not deleted: ") + e.what();
  }
  outOK = true;
  return "Deleted " + Count(inEntryPks.size(), "entry", "entries") + " from the backup";
}


std::string ExportEntries(const std::filesystem::path & inFile, int64_t inAppPk, int64_t inStorePk,
                          const std::filesystem::path & inFolder, const std::vector<int64_t> & inEntryPks, bool & outOK)
{
  outOK = false;
  std::string error;
  std::unique_ptr<DeviceFile> file = DeviceFile::Open(inFile, DeviceFile::kReadOnly, error);
  if (!file)
    return "Not exported: " + error;
  try {
    const AppRecord * app = nullptr;
    std::vector<AppRecord> apps = file->apps();
    for (const AppRecord & each : apps)
      if (each.pk == inAppPk)
        app = &each;
    if (!app)
      return "Not exported: the app is gone";
    ExportResult result;
    CallNewton([&] { result = ExportApp(*file, *app, inStorePk, inFolder, inEntryPks); });
    outOK = true;
    return "Exported " + Count(result.entries, "entry", "entries") + " into "
         + Count(result.files.size(), "file", "files") + " in " + UTF8FromPath(inFolder);
  } catch (const std::exception & e) {
    return std::string("Not exported: ") + e.what();
  }
}

} // namespace ncx
