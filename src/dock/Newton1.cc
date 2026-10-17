/*
 File: Newton1.cc

 The NewtonOS 1 session (PORTING.md 5.2.8; the Cocoa app's
 NCNewton1Component): its backup (Backup.h) and restore (Restore.h). A
 NewtonOS 1 Newton speaks the old protocol: no apps, no sync options, no
 desktop control; the desktop asks, and ends the session when it's done.
 */

#include "dock/Backup.h"
#include "dock/Restore.h"
#include "dock/DockController.h"
#include "base/Log.h"
#include "base/NewtonStrings.h"
#include "comms/DockRefs.h"
#include "model/DeviceFile.h"
#include "model/NewtonModel.h"

#include "RSSymbols.h"

namespace ncx {

/*-- backup ------------------------------------------------------------------*/

int Backup::backupNewton1(Progress * ioProgress)
{
  mCounts = BackupCounts();
  mPlan = DeletionPlan();
  Session & session = mDock.session();
  if (ioProgress) {
    ioProgress->setTotal(-1);
    ioProgress->setDescription("Fetching the stores");
  }
  RefVar stores(session.getAllStores());
  if (!IsArray(stores)) {
    done(kDockErrStoreNotFound);
    return kDockErrStoreNotFound;
  }
  // the soups of every store, for the progress: [names, signatures]
  int64_t total = 0;
  for (ArrayIndex i = 0, count = Length(stores); i < count; ++i) {
    RefVar store(GetArraySlot(stores, i));
    session.setCurrentStore(store, false);
    RefVar soups(session.getAllSoups());
    SetFrameSlot(store, SYMA(soups), soups);
    if (IsArray(soups) && IsArray(GetArraySlot(soups, 0)))
      total += Length(GetArraySlot(soups, 0));
  }
  if (ioProgress) {
    ioProgress->setTotal(total);
    ioProgress->setCompleted(0);
  }

  int error = 0;
  int64_t soupsDone = 0;
  std::optional<DeviceRecord> device = mFile.device();
  for (ArrayIndex i = 0, count = Length(stores); i < count && error == 0; ++i) {
    RefVar store(GetArraySlot(stores, i));
    // the store (UpdateDevice made it)
    StoreRecord storeRecord;
    RefVar signature(GetFrameSlot(store, MakeSymbol("signature")));
    bool known = false;
    for (const StoreRecord & s : mFile.stores())
      if (ISINT(signature) && s.signature == RINT(signature)) {
        storeRecord = s;
        known = true;
      }
    if (!known) {
      DeviceFile::Transaction transaction(mFile);
      storeRecord = AddStore(mFile, device ? device->pk : 0, store);
      transaction.commit();
    }
    session.setCurrentStore(store, false);
    RefVar soups(GetFrameSlot(store, SYMA(soups)));
    RefVar names(IsArray(soups) ? GetArraySlot(soups, 0) : (Ref)NILREF);
    RefVar signatures(IsArray(soups) ? GetArraySlot(soups, 1) : (Ref)NILREF);
    for (ArrayIndex s = 0, soupCount = IsArray(names) ? Length(names) : 0; s < soupCount && error == 0; ++s) {
      RefVar soupName(GetArraySlot(names, s));
      if (!IsString(soupName) || session.setCurrentSoup(soupName) != 0) {
        Log(kLogMinimum, "backup: no soup %s on the store", IsString(soupName) ? UTF8FromString(soupName).c_str() : "?");
        continue;
      }
      if (ioProgress)
        ioProgress->setDescription(storeRecord.name + ": " + UTF8FromString(soupName));
      RefVar soupSignature(IsArray(signatures) && s < Length(signatures) ? GetArraySlot(signatures, s) : (Ref)NILREF);
      error = backupNewton1Soup(storeRecord, soupName, ISINT(soupSignature) ? RINT(soupSignature) : 0, ioProgress);
      if (ioProgress) {
        ioProgress->setCompleted(++soupsDone);
        // cancelled: after a whole soup (as the Cocoa app)
        if (error == 0 && ioProgress->cancelled())
          error = kDockErrAborted;
      }
    }
  }
  if (error == 0)
    deleteOnNewton();
  done(error);
  return error;
}


int Backup::backupNewton1Soup(const StoreRecord & inStore, RefArg inSoupName, int64_t inSignature, Progress * ioProgress)
{
  Session & session = mDock.session();
  std::string name = UTF8FromString(inSoupName);
  DeviceFile::Transaction transaction(mFile);
  std::optional<SoupRecord> found = mFile.findSoup(inStore.pk, name);
  // its backup finished once (NCKLastBackupTime): only what changed since
  bool complete = found && found->lastBackupTime != 0;
  uint32_t syncTime = session.setLastSyncTime(complete ? (uint32_t)found->lastBackupTime : 0);

  // its info and indexes
  DockEvent answer = session.sendAndReceive(kDGetSoupInfo, kDAnyEvent);
  if (answer.command == kDOperationCanceled) {
    session.send(kDOpCanceledAck);
    return kDockErrAborted;
  }
  RefVar info(answer.command == kDSoupInfo ? EventRef(answer) : (Ref)NILREF);
  answer = session.sendAndReceive(kDGetIndexDescription, kDAnyEvent);
  if (answer.command == kDOperationCanceled) {
    session.send(kDOpCanceledAck);
    return kDockErrAborted;
  }
  RefVar indexes(answer.command == kDIndexDescription ? EventRef(answer) : (Ref)NILREF);
  SoupRecord soup;
  if (found) {
    soup = *found;
    if (IsFrame(info))
      SetSoupInfo(soup, info);
    if (IsArray(indexes))
      SetSoupIndexes(soup, indexes);
    soup.signature = inSignature;
    mFile.update(soup);
  } else {
    // NewtonOS 1 has no apps: one of the soup's name (or the NewtonOS 2 one)
    soup = MakeSoup(inStore.pk, name, inSignature, indexes, info);
    soup.appPk = FindOrAddApp(mFile, Newton1AppName(name)).pk;
    mFile.insert(soup);
  }

  // the ids of all its entries, and what to fetch: what changed, or all
  BackupIds ids;
  RefVar all(session.getEntryIds());
  for (ArrayIndex i = 0, count = IsArray(all) ? Length(all) : 0; i < count; ++i)
    ids.add(RINT(GetArraySlot(all, i)));
  RefVar fetch(complete ? session.getChangedIds() : (Ref)all);
  for (ArrayIndex i = 0, count = IsArray(fetch) ? Length(fetch) : 0; i < count; ++i) {
    session.sendValue(kDReturnEntry, (int32_t)RINT(GetArraySlot(fetch, i)));
    DockEvent event = session.receive(kDAnyEvent);
    if (event.command == kDEntry) {
      RefVar entry(EventRef(event));
      if (IsFrame(entry)) {
        AddEntry(mFile, soup, entry, event.data);
        ++mCounts.entries;
      }
    } else if (event.command == kDOperationCanceled) {
      session.send(kDOpCanceledAck);
      transaction.commit();         // what came stays
      return kDockErrAborted;
    } else {
      Log(kLogMinimum, "backup: '%s' instead of entry %d of %s", CmdName(event.command).c_str(),
          (int)RINT(GetArraySlot(fetch, i)), name.c_str());
    }
  }
  if (ioProgress && ioProgress->cancelled()) {
    transaction.commit();
    return kDockErrAborted;
  }

  // what the Newton no longer has leaves the file; then synchronised
  mCounts.removed += mFile.cropSoup(soup.pk, std::vector<int64_t>(ids.ids().begin(), ids.ids().end()));
  synchronise(soup, inStore, complete, ids);

  // the time of this backup in the soup's info, here and on the Newton
  if (!IsFrame(info))
    info = SoupInfoFrame(soup);
  SetFrameSlot(info, MakeSymbol("NCKLastBackupTime"), MAKEINT((int64_t)syncTime + 1));
  SetSoupInfo(soup, info);
  mFile.update(soup);
  session.setSoupInfo(info);
  ++mCounts.soups;
  transaction.commit();
  return 0;
}


/*-- restore -----------------------------------------------------------------*/

int Restore::restoreNewton1(const RestoreSelection & inSelection, Progress * ioProgress)
{
  // no request, no source version, no control: the store, then the soups
  Session & session = mDock.session();
  int error = session.setCurrentStore(RefVar(StoreFrame(inSelection.target)), false);
  if (ioProgress) {
    ioProgress->setTotal((int64_t)inSelection.apps.size());
    ioProgress->setCompleted(0);
  }
  int64_t done = 0;
  for (size_t a = 0; a < inSelection.apps.size() && error == 0; ++a) {
    const AppRecord & app = inSelection.apps[a];
    for (SoupRecord soup : mFile.soups(inSelection.source.pk)) {
      if (soup.appPk != app.pk)
        continue;
      if (ioProgress)
        ioProgress->setDescription(inSelection.target.name + ": " + soup.name);
      error = restoreSoup(soup, ioProgress);
      if (error != 0)
        break;
    }
    if (ioProgress)
      ioProgress->setCompleted(++done);
  }
  // a NewtonOS 1 backup has no packages (as the Cocoa app)
  if (!inSelection.packages.empty())
    Log(kLogMinimum, "restore: NewtonOS 1: %d packages left out", (int)inSelection.packages.size());
  return error;
}

} // namespace ncx
