/*
 File: Edits.cc

 What the UI does to a backup file without a Newton (Edits.h).
 */

#include "ui/Edits.h"

#include "base/NewtonCall.h"
#include "model/DeviceFile.h"
#include "model/NewtonModel.h"
#include "platform/AppFolders.h"
#include "translators/Transfer.h"

#include "RSSymbols.h"

#include <algorithm>
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

std::string ImportIntoFile(const std::filesystem::path & inFile, int64_t inStorePk, const std::vector<ImportItem> & inItems,
                           std::vector<int64_t> & outEntryPks, std::vector<int64_t> & outSoupPks, bool & outOK)
{
  outOK = false;
  std::string error;
  std::unique_ptr<DeviceFile> file = DeviceFile::Open(inFile, DeviceFile::kReadWrite, error);
  if (!file)
    return "Not imported: " + error;
  size_t entries = 0, skipped = 0;
  std::string failed;
  try {
    std::optional<StoreRecord> store;
    for (const StoreRecord & each : file->stores())
      if (each.pk == inStorePk)
        store = each;
    if (!store)
      return "Not imported: the store is gone";
    for (const ImportItem & item : inItems) {
      ImportResult result;
      CallNewton([&] { result = ImportFile(*file, *store, item.app, item.source); });
      if (!result.error.empty()) {
        failed += (failed.empty() ? "" : "; ") + result.error;
        continue;
      }
      entries += result.entries;
      skipped += result.skipped;
      outEntryPks.insert(outEntryPks.end(), result.entryPks.begin(), result.entryPks.end());
      for (const auto & soup : result.soups)
        if (std::optional<SoupRecord> record = file->findSoup(store->pk, soup.first))
          if (std::find(outSoupPks.begin(), outSoupPks.end(), record->pk) == outSoupPks.end())
            outSoupPks.push_back(record->pk);
    }
  } catch (const std::exception & e) {
    return std::string("Not imported: ") + e.what();
  }
  outOK = failed.empty();
  std::string report = "Imported " + Count(entries, "entry", "entries");
  if (skipped)
    report += " (" + Count(skipped, "entry", "entries") + " left out: their soup isn't on the store)";
  if (!failed.empty())
    report += "; " + failed;
  return report;
}


std::string CopyEntries(const std::filesystem::path & inSource, const std::vector<int64_t> & inEntryPks,
                        const std::filesystem::path & inFile, int64_t inStorePk,
                        std::vector<int64_t> & outEntryPks, std::vector<int64_t> & outSoupPks, bool & outOK)
{
  outOK = false;
  std::string error;
  std::unique_ptr<DeviceFile> source = DeviceFile::Open(inSource, DeviceFile::kReadOnly, error);
  if (!source)
    return "Not copied: " + error;
  std::unique_ptr<DeviceFile> file = DeviceFile::Open(inFile, DeviceFile::kReadWrite, error);
  if (!file)
    return "Not copied: " + error;
  size_t copied = 0, skipped = 0;
  try {
    DeviceFile::Transaction transaction(*file);
    for (int64_t pk : inEntryPks) {
      std::optional<EntryRecord> entry = source->entry(pk, true);
      std::optional<SoupRecord> from = entry ? source->soup(entry->soupPk) : std::nullopt;
      std::optional<SoupRecord> to = from ? file->findSoup(inStorePk, from->name) : std::nullopt;
      if (!to) {
        ++skipped;
        continue;
      }
      CallNewton([&] {
        RefVar frame(EntryFrame(*entry));
        if (!IsFrame(frame))
          return;
        // a new entry here: a new id, from kImportIdBase on (AddImportedEntry)
        RemoveSlot(frame, SYMA(_uniqueId));
        EntryRecord made = AddImportedEntry(*file, *to, frame);
        outEntryPks.push_back(made.pk);
        ++copied;
      });
      if (std::find(outSoupPks.begin(), outSoupPks.end(), to->pk) == outSoupPks.end())
        outSoupPks.push_back(to->pk);
    }
    transaction.commit();
  } catch (const std::exception & e) {
    outEntryPks.clear();
    return std::string("Not copied: ") + e.what();
  }
  outOK = true;
  std::string report = "Copied " + Count(copied, "entry", "entries");
  if (skipped)
    report += " (" + Count(skipped, "entry", "entries") + " left out: their soup isn't on the store)";
  return report;
}


std::vector<std::string> ImportAppsForFile(const std::filesystem::path & inSource)
{
  return ImportAppsFor(inSource);
}

} // namespace ncx
