/*
 File: SoupEditor.cc

 Changing the docked Newton's soups from the desktop (SoupEditor.h).
 */

#include "dock/SoupEditor.h"
#include "dock/DockController.h"
#include "base/Log.h"
#include "base/NewtonStrings.h"
#include "model/DeviceFile.h"
#include "model/NewtonModel.h"

#include "RSSymbols.h"

#include <algorithm>

namespace ncx {

int SoupEditor::selectSoup(int64_t inSoupPk)
{
  // the soup's store on the Newton (if it has it), then the soup
  std::optional<SoupRecord> soup = mFile.soup(inSoupPk);
  if (!soup)
    return kDockErrSoupNotFound;
  std::vector<StoreRecord> stores = mFile.stores();
  auto store = std::find_if(stores.begin(), stores.end(), [&](const StoreRecord & s) { return s.pk == soup->storePk; });
  if (store == stores.end())
    return kDockErrStoreNotFound;
  Session & session = mDock.session();
  int error = session.setCurrentStore(RefVar(StoreFrame(*store)), false);
  if (error == 0)
    error = session.setCurrentSoup(RefVar(MakeStringFromUTF8(soup->name)));
  return error;
}


int SoupEditor::add(int64_t inSoupPk, const std::vector<int64_t> & inEntryPks)
{
  mCounts = SoupEditCounts();
  std::optional<SoupRecord> soup = mFile.soup(inSoupPk);
  if (!soup)
    return kDockErrSoupNotFound;
  std::vector<EntryRecord> entries;
  for (EntryRecord & entry : mFile.entries(inSoupPk, true))
    if (entry.uniqueId >= kImportIdBase
        && (inEntryPks.empty() || std::find(inEntryPks.begin(), inEntryPks.end(), entry.pk) != inEntryPks.end()))
      entries.push_back(std::move(entry));
  if (entries.empty())
    return 0;

  mDock.setDesktopControl(true);
  int error = selectSoup(inSoupPk);
  if (error == 0) {
    DeviceFile::Transaction transaction(mFile);
    Session & session = mDock.session();
    for (EntryRecord & entry : entries) {
      RefVar frame(EntryFrame(entry));
      if (!IsFrame(frame))
        continue;
      session.addEntry(frame);          // it gets the Newton's _uniqueId
      UpdateEntry(mFile, *soup, entry, frame);
      ++mCounts.added;
    }
    transaction.commit();
  }
  mDock.setDesktopControl(false);
  Log(kLogMinimum, "edit: %s: %d entries made on the desktop sent to the Newton", soup->name.c_str(), (int)mCounts.added);
  return error;
}


int SoupEditor::remove(int64_t inSoupPk, const std::vector<int64_t> & inEntryPks)
{
  mCounts = SoupEditCounts();
  std::optional<SoupRecord> soup = mFile.soup(inSoupPk);
  if (!soup)
    return kDockErrSoupNotFound;
  // the Newton's entries by id, its packages by name; those made on the
  // desktop it never had
  std::vector<EntryRecord> onNewton, packages, desktopOnly;
  for (int64_t pk : inEntryPks) {
    std::optional<EntryRecord> entry = mFile.entry(pk, true);
    if (!entry || entry->soupPk != inSoupPk)
      continue;
    if (entry->uniqueId >= kImportIdBase)
      desktopOnly.push_back(std::move(*entry));
    else if (entry->refClass == std::string("*package*"))
      packages.push_back(std::move(*entry));
    else
      onNewton.push_back(std::move(*entry));
  }

  int error = 0;
  if (!onNewton.empty() || !packages.empty()) {
    Session & session = mDock.session();
    mDock.setDesktopControl(true);
    if (!onNewton.empty()) {
      error = selectSoup(inSoupPk);
      if (error == 0) {
        RefVar ids(MakeArray(0));
        for (const EntryRecord & entry : onNewton)
          AddArraySlot(ids, MAKEINT(entry.uniqueId));
        error = session.deleteEntryIdList(ids);
      }
      if (error == 0) {
        DeviceFile::Transaction transaction(mFile);
        for (const EntryRecord & entry : onNewton)
          mFile.deleteEntry(entry.pk);
        transaction.commit();
        mCounts.deleted = onNewton.size();
      }
    }
    if (error == 0 && !packages.empty()) {
      // the package's store, then the default one (as the Cocoa app: "yes,
      // you really need to do this"); then each by name
      std::vector<StoreRecord> stores = mFile.stores();
      auto store = std::find_if(stores.begin(), stores.end(), [&](const StoreRecord & s) { return s.pk == soup->storePk; });
      if (store != stores.end())
        error = session.setCurrentStore(RefVar(StoreFrame(*store)), false);
      if (error == 0)
        error = session.setCurrentStore(RefVar(NILREF), false);
      for (size_t i = 0; i < packages.size() && error == 0; ++i) {
        RefVar frame(EntryFrame(packages[i]));
        RefVar name(IsFrame(frame) ? GetFrameSlot(frame, MakeSymbol("packageName")) : (Ref)NILREF);
        if (!IsString(name))
          continue;
        session.sendRef(kDRemovePackage, name);
        error = session.receiveResult();
        if (error != 0) {
          Log(kLogMinimum, "edit: the Newton didn't remove package %s: %d", UTF8FromString(name).c_str(), error);
          break;
        }
        DeviceFile::Transaction transaction(mFile);
        mFile.deleteEntry(packages[i].pk);
        transaction.commit();
        ++mCounts.packages;
      }
    }
    mDock.setDesktopControl(false);
  }

  if (!desktopOnly.empty()) {
    DeviceFile::Transaction transaction(mFile);
    for (const EntryRecord & entry : desktopOnly)
      mFile.deleteEntry(entry.pk);
    transaction.commit();
    mCounts.fileOnly = desktopOnly.size();
  }
  Log(kLogMinimum, "edit: %s: %d entries and %d packages deleted on the Newton, %d made on the desktop deleted",
      soup->name.c_str(), (int)mCounts.deleted, (int)mCounts.packages, (int)mCounts.fileOnly);
  return error;
}

} // namespace ncx
