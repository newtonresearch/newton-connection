/*
 File: Transfer.cc

 Exporting and importing entries of a .newtondevice file (Transfer.h).
 */

#include "translators/Transfer.h"
#include "translators/Translator.h"
#include "model/DeviceFile.h"
#include "model/NewtonModel.h"
#include "platform/AppFolders.h"

#include <algorithm>

namespace ncx {

ExportResult ExportApp(DeviceFile & inFile, const AppRecord & inApp, int64_t inStorePk, const std::filesystem::path & inFolder,
                       const std::vector<int64_t> & inEntryPks, RichTextFormat inRichText)
{
  ExportResult result;
  TranslatorContext context;
  context.folder = inFolder;
  context.appName = inApp.name;
  context.richText = inRichText;
  ExportSession session(context);
  for (const SoupRecord & soup : inFile.soups(inStorePk)) {
    if (soup.appPk != inApp.pk)
      continue;
    for (const EntryRecord & entry : inFile.entries(soup.pk, true)) {
      if (!inEntryPks.empty() && std::find(inEntryPks.begin(), inEntryPks.end(), entry.pk) == inEntryPks.end())
        continue;
      RefVar frame(EntryFrame(entry));
      if (!IsFrame(frame))
        continue;
      session.add(frame);
      ++result.entries;
    }
  }
  result.files = session.end();
  return result;
}


ImportResult ImportFile(DeviceFile & ioFile, const StoreRecord & inStore, const std::string & inApp,
                        const std::filesystem::path & inSource)
{
  ImportResult result;
  const ImportFormat * format = FindImportFormat(inApp, inSource);
  if (!format) {
    result.error = inApp + " doesn't import " + UTF8FromPath(inSource.filename());
    return result;
  }
  TranslatorContext context;
  context.appName = inApp;
  std::unique_ptr<Importer> importer = format->make();
  if (!importer->begin(inSource, context)) {
    result.error = UTF8FromPath(inSource.filename()) + ": " + importer->why();
    return result;
  }
  DeviceFile::Transaction transaction(ioFile);
  for (;;) {
    std::string soupName = format->soup;
    RefVar entry(importer->next(soupName));
    if (ISNIL(entry))
      break;
    std::optional<SoupRecord> soup = ioFile.findSoup(inStore.pk, soupName);
    if (!soup) {
      ++result.skipped;
      continue;
    }
    EntryRecord record = AddImportedEntry(ioFile, *soup, entry);
    result.entryPks.push_back(record.pk);
    ++result.soups[soupName];
    ++result.entries;
  }
  transaction.commit();
  importer->end();
  return result;
}

} // namespace ncx
