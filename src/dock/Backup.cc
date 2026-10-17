/*
 File: Backup.cc

 Backing up a Newton into its .newtondevice file (Backup.h).
 */

#include "dock/Backup.h"
#include "dock/DockController.h"
#include "base/Dates.h"
#include "base/Log.h"
#include "base/NewtonStrings.h"
#include "comms/DockRefs.h"
#include "model/DeviceFile.h"
#include "model/DeviceList.h"
#include "model/NewtonModel.h"

#include "RSSymbols.h"

#include <algorithm>
#include <cstdio>

namespace ncx {

namespace {

std::string Lower(std::string inText)
{
  std::transform(inText.begin(), inText.end(), inText.begin(), [](char c) { return (char)std::tolower((unsigned char)c); });
  return inText;
}

/** Where a string is in an array of them (without case, as StrEqual): -1
    if it isn't. */
int IndexOf(RefArg inArray, RefArg inString)
{
  if (!IsArray(inArray) || !IsString(inString))
    return -1;
  std::string wanted = Lower(UTF8FromString(inString));
  for (ArrayIndex i = 0, count = Length(inArray); i < count; ++i) {
    RefVar item(GetArraySlot(inArray, i));
    if (IsString(item) && Lower(UTF8FromString(item)) == wanted)
      return (int)i;
  }
  return -1;
}

bool IsPackagesApp(RefArg inApp)
{
  // NewtonOS 2.1: class 'packageFrame; 2.0: a packages slot
  return EQRef(ClassOf(inApp), MakeSymbol("packageFrame")) || NOTNIL(GetFrameSlot(inApp, MakeSymbol("packages")));
}

bool IsSystemApp(RefArg inApp)
{
  return EQRef(ClassOf(inApp), MakeSymbol("systemFrame"));
}

} // namespace


/*-- the device --------------------------------------------------------------*/

void UpdateDevice(DockController & ioDock, DeviceFile & ioFile, bool inSetClock)
{
  Session & session = ioDock.session();
  if (ioDock.newton().newton1) {
    // NewtonOS 1: no control, no Gestalt, no global functions; its stores
    RefVar stores(session.getAllStores());
    DeviceFile::Transaction transaction(ioFile);
    DeviceRecord device = SetDevice(ioFile, ioDock.newton().name, ioDock.newton().info);
    for (ArrayIndex i = 0, count = IsArray(stores) ? Length(stores) : 0; i < count; ++i)
      AddStore(ioFile, device.pk, RefVar(GetArraySlot(stores, i)));
    transaction.commit();
    return;
  }
  ioDock.setDesktopControl(true);
  RefVar stores(session.getAllStores());
  {
    DeviceFile::Transaction transaction(ioFile);
    DeviceRecord device = SetDevice(ioFile, ioDock.newton().name, ioDock.newton().info);
    for (ArrayIndex i = 0, count = IsArray(stores) ? Length(stores) : 0; i < count; ++i)
      AddStore(ioFile, device.pk, RefVar(GetArraySlot(stores, i)));

    // what the Newton is, more than NewtonInfo says
    if (!device.osInfo) {
      RefVar gestalt(session.getGestalt(0x01000003));      // kGestalt_SystemInfo
      if (IsFrame(gestalt)) {
        RefVar version(GetFrameSlot(gestalt, MakeSymbol("ROMversionString")));
        RefVar patch(GetFrameSlot(gestalt, MakeSymbol("patchVersion")));
        device.osInfo = IsString(version) && ISINT(patch)
                      ? UTF8FromString(version) + "-" + std::to_string(RINT(patch)) : std::string("?");
        RefVar made(GetFrameSlot(gestalt, MakeSymbol("manufactureDate")));
        if (ISINT(made))
          device.manufactureDate = CoreDataFromTime(TimeFromNewton(RINT(made)));
        RefVar cpu(GetFrameSlot(gestalt, MakeSymbol("CPUtype")));
        RefVar speed(GetFrameSlot(gestalt, MakeSymbol("CPUspeed")));
        if (IsSymbol(cpu)) {
          std::string type = Lower(UTF8FromSymbol(cpu));
          std::string name = type == "strongarm" ? "StrongARM" : type == "arm710a" ? "ARM 710"
                           : type == "arm610a" ? "ARM 610" : UTF8FromSymbol(cpu);
          if (IsReal(speed)) {
            char text[64];
            std::snprintf(text, sizeof(text), "%.1f MHz  %s", CDouble(speed), name.c_str());
            name = text;
          }
          device.processor = name;
        }
        ioFile.update(device);
      }
    }
    transaction.commit();
  }
  if (inSetClock)
    session.setDateTime(RefVar(MAKEINT(NewtonFromTime(Now()))));
  session.getHexade();
  ioDock.setDesktopControl(false);
}


int SendImportedEntry(DockController & ioDock, DeviceFile & ioFile, SoupRecord & ioSoup, EntryRecord & ioEntry)
{
  Session & session = ioDock.session();
  RefVar frame(EntryFrame(ioEntry));
  if (!IsFrame(frame))
    return 0;
  session.sendRef(kDAddEntry, frame);
  DockEvent answer = session.receive(kDAnyEvent);
  if (answer.command == kDAddedID) {
    SetFrameSlot(frame, SYMA(_uniqueId), MAKEINT(answer.value()));
    SetFrameSlot(frame, SYMA(_modTime), MAKEINT(session.currentTime()));
    UpdateEntry(ioFile, ioSoup, ioEntry, frame);
    return 0;
  }
  if (answer.command == kDResult && answer.value() == 0 && ioDock.newton().newton1) {
    ioFile.deleteEntry(ioEntry.pk);
    return 0;
  }
  if (answer.command == kDOperationCanceled) {
    session.send(kDOpCanceledAck);
    return kDockErrAborted;
  }
  int error = answer.command == kDResult ? answer.value() : kDockErrProtocolError;
  Log(kLogMinimum, "the Newton didn't add an entry made on the desktop ('%s' %d)", CmdName(answer.command).c_str(), error);
  return error;
}


/*-- the ids -----------------------------------------------------------------*/

void BackupIds::addCoded(const std::vector<uint8_t> & inData)
{
  for (size_t i = 0; i + 1 < inData.size(); i += 2) {
    int16_t code = (int16_t)(inData[i] << 8 | inData[i + 1]);
    if (code == (int16_t)0x8000)
      break;
    if (code < 0) {
      for (int k = 1; k <= -code; ++k)
        mIds.insert(mBase + mRunBase + k);
    } else {
      mRunBase = code;
      mIds.insert(mBase + code);
    }
  }
}


/*-- the backup --------------------------------------------------------------*/

int Backup::run(Progress * ioProgress)
{
  if (mDock.newton().newton1)
    return backupNewton1(ioProgress);
  mDock.setDesktopControl(true);
  mDock.session().send(kDRequestToSync);
  int error = mDock.session().receiveResult();
  if (error != 0) {
    done(error);
    return error;
  }
  return backup(ioProgress);
}


int Backup::answer(Progress * ioProgress)
{
  return backup(ioProgress);
}


int Backup::backup(Progress * ioProgress)
{
  mCounts = BackupCounts();
  mPlan = DeletionPlan();
  Session & session = mDock.session();
  if (ioProgress) {
    ioProgress->setTotal(-1);
    ioProgress->setDescription("Fetching sync options");
  }
  RefVar syncInfo(EventRef(session.sendAndReceive(kDGetSyncOptions, kDSyncOptions)));
  session.setLastSyncTime(0);
  bool packages = IsFrame(syncInfo) && NOTNIL(GetFrameSlot(syncInfo, MakeSymbol("packages")));
  RefVar stores(IsFrame(syncInfo) ? GetFrameSlot(syncInfo, MakeSymbol("stores")) : (Ref)NILREF);
  if (!IsArray(stores)) {
    done(kDockErrStoreNotFound);
    return kDockErrStoreNotFound;
  }

  // the apps of every store, for the progress: [{name, soups}, ...]
  int64_t apps = 0;
  for (ArrayIndex i = 0, count = Length(stores); i < count; ++i) {
    RefVar store(GetArraySlot(stores, i));
    RefVar storeApps(GetFrameSlot(store, MakeSymbol("apps")));
    if (!IsArray(storeApps)) {
      // [{name, soups}, ...] of the apps on this store
      session.setCurrentStore(store, false);
      session.sendValue(kDGetAppNames, kNamesAndSoupsForThisStore);
      storeApps = EventRef(session.receive(kDAppNames));
      SetFrameSlot(store, MakeSymbol("apps"), storeApps);
    }
    apps += IsArray(storeApps) ? Length(storeApps) : 0;
  }
  if (ioProgress) {
    ioProgress->setTotal(apps);
    ioProgress->setCompleted(0);
  }

  int error = 0;
  int64_t appsDone = 0;
  std::optional<DeviceRecord> device = mFile.device();
  for (ArrayIndex i = 0, count = Length(stores); i < count && error == 0; ++i) {
    RefVar store(GetArraySlot(stores, i));
    // the store (UpdateDevice made it, with its sizes; the sync options'
    // frame may not have them)
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

    // the soups' names and signatures, from the store (or asked for)
    RefVar soupNames(GetFrameSlot(store, SYMA(soups)));
    RefVar signatures(GetFrameSlot(store, MakeSymbol("signatures")));
    if (!IsArray(signatures)) {
      RefVar soups(session.getAllSoups());
      soupNames = GetArraySlot(soups, 0);
      signatures = GetArraySlot(soups, 1);
    }

    // System information first, Packages last (if they are wanted)
    RefVar storeApps(GetFrameSlot(store, MakeSymbol("apps")));
    if (!IsArray(storeApps))
      continue;
    // (indices into storeApps: Refs in a vector aren't safe from the GC)
    std::vector<ArrayIndex> order;
    ArrayIndex systemAt = (ArrayIndex)-1, packagesAt = (ArrayIndex)-1;
    for (ArrayIndex k = 0, n = Length(storeApps); k < n; ++k) {
      RefVar app(GetArraySlot(storeApps, k));
      if (IsPackagesApp(app))
        packagesAt = k;
      else if (IsSystemApp(app) && systemAt == (ArrayIndex)-1)
        systemAt = k;
    }
    if (systemAt != (ArrayIndex)-1)
      order.push_back(systemAt);
    for (ArrayIndex k = 0, n = Length(storeApps); k < n; ++k)
      if (k != systemAt && k != packagesAt)
        order.push_back(k);
    if (packagesAt != (ArrayIndex)-1 && packages)
      order.push_back(packagesAt);

    for (size_t k = 0; k < order.size() && error == 0; ++k) {
      RefVar app(GetArraySlot(storeApps, order[k]));
      bool isPackages = IsPackagesApp(app);
      AppRecord appRecord;
      {
        DeviceFile::Transaction transaction(mFile);
        appRecord = FindOrAddApp(mFile, UTF8FromString(GetFrameSlot(app, SYMA(name))));
        transaction.commit();
      }
      RefVar appSoups(GetFrameSlot(app, SYMA(soups)));
      for (ArrayIndex s = 0, soupCount = IsArray(appSoups) ? Length(appSoups) : 0; s < soupCount && error == 0; ++s) {
        RefVar soupName(GetArraySlot(appSoups, s));
        if (!IsString(soupName) || session.setCurrentSoup(soupName) != 0) {
          Log(kLogMinimum, "backup: no soup %s on the store", IsString(soupName) ? UTF8FromString(soupName).c_str() : "?");
          continue;
        }
        if (ioProgress)
          ioProgress->setDescription(storeRecord.name + ": " + appRecord.name
                                     + (soupCount > 1 ? " (" + UTF8FromString(soupName) + ")" : std::string()));
        int at = IndexOf(soupNames, soupName);
        int64_t signature = at >= 0 && ISINT(GetArraySlot(signatures, (ArrayIndex)at))
                          ? RINT(GetArraySlot(signatures, (ArrayIndex)at)) : 0;
        error = backupSoup(storeRecord, appRecord, isPackages, soupName, signature, ioProgress);
      }
      if (ioProgress)
        ioProgress->setCompleted(++appsDone);
    }
  }
  // what the file lacks, deleted on the Newton if that's clear or wanted
  if (error == 0)
    deleteOnNewton();
  done(error);
  return error;
}


int Backup::receiveEntries(SoupRecord & ioSoup, bool inPackages, BackupIds * ioIds, Progress * ioProgress,
                           EventType & outEnd)
{
  Session & session = mDock.session();
  for (;;) {
    DockEvent event = session.receive(kDAnyEvent);
    if (event.command == kDEntry) {
      RefVar entry(EventRef(event));
      if (!IsFrame(entry))
        continue;
      // in the Packages soup only the packages themselves
      if (inPackages && !FrameHasSlot(entry, MakeSymbol("pkgRef")))
        continue;
      if (inPackages && ioProgress)
        ioProgress->setDescription("Package " + UTF8FromString(GetFrameSlot(entry, MakeSymbol("packageName"))));
      EntryRecord record = AddEntry(mFile, ioSoup, entry, event.data);
      ++mCounts.entries;
      if (ioIds)
        ioIds->add(record.uniqueId);
    } else if (event.command == kDSetBaseID) {
      if (ioIds)
        ioIds->setBase(event.value());
    } else if (event.command == kDBackupIDs) {
      if (ioIds)
        ioIds->addCoded(event.data);
    } else if (event.command == kDBackupSoupDone || event.command == kDSoupNotDirty) {
      outEnd = event.command;
      return 0;
    } else if (event.command == kDOperationCanceled) {
      session.send(kDOpCanceledAck);
      return kDockErrAborted;
    } else {
      Log(kLogMinimum, "backup: '%s' while backing up a soup, ignored", CmdName(event.command).c_str());
    }
    if (ioProgress && ioProgress->cancelled()) {
      // the Newton doesn't answer this one (as the Cocoa app)
      session.send(kDOperationCanceled);
      return kDockErrAborted;
    }
  }
}


int Backup::backupSoup(const StoreRecord & inStore, const AppRecord & inApp, bool inPackages, RefArg inSoupName,
                       int64_t inSignature, Progress * ioProgress)
{
  Session & session = mDock.session();
  std::string name = UTF8FromString(inSoupName);
  DeviceFile::Transaction transaction(mFile);
  std::optional<SoupRecord> found = mFile.findSoup(inStore.pk, name);
  RefVar info;
  uint32_t syncTime;
  int error = 0;
  EventType end = 0;

  if (found) {
    SoupRecord soup = *found;
    // its backup finished once (NCKLastBackupTime), else the file may lack
    // some of it
    bool complete = soup.lastBackupTime != 0;
    syncTime = session.setLastSyncTime((uint32_t)soup.lastBackupTime);
    // its info and indexes, if they changed
    DockEvent changed = session.sendAndReceive(kDGetChangedInfo, kDAnyEvent);
    if (changed.command == kDOperationCanceled) {
      session.send(kDOpCanceledAck);
      return kDockErrAborted;
    }
    if (changed.command == kDSoupInfo) {
      info = EventRef(changed);
      SetSoupInfo(soup, info);
    }
    changed = session.sendAndReceive(kDGetChangedIndex, kDAnyEvent);
    if (changed.command == kDOperationCanceled) {
      session.send(kDOpCanceledAck);
      return kDockErrAborted;
    }
    if (changed.command == kDIndexDescription)
      SetSoupIndexes(soup, RefVar(EventRef(changed)));
    soup.signature = inSignature;
    mFile.update(soup);

    // what changed since the last backup, and the ids of the rest
    BackupIds ids;
    session.sendValue(kDBackupSoup, (int32_t)soup.lastBackupId);
    error = receiveEntries(soup, inPackages, &ids, ioProgress, end);
    if (error == 0 && end == kDBackupSoupDone)
      mCounts.removed += mFile.cropSoup(soup.pk, std::vector<int64_t>(ids.ids().begin(), ids.ids().end()));
    // packages can't be deleted or made on the desktop: nothing to send back
    if (error == 0 && !inPackages)
      synchronise(soup, inStore, complete, ids);
    found = soup;
  } else {
    // a soup new to the file: all of it
    syncTime = session.setLastSyncTime(0);
    DockEvent answer = session.sendAndReceive(kDGetSoupInfo, kDAnyEvent);
    if (answer.command == kDOperationCanceled) {
      session.send(kDOpCanceledAck);
      return kDockErrAborted;
    }
    if (answer.command == kDSoupInfo)
      info = EventRef(answer);
    answer = session.sendAndReceive(kDGetIndexDescription, kDAnyEvent);
    if (answer.command == kDOperationCanceled) {
      session.send(kDOpCanceledAck);
      return kDockErrAborted;
    }
    RefVar indexes(answer.command == kDIndexDescription ? EventRef(answer) : (Ref)NILREF);
    SoupRecord soup = MakeSoup(inStore.pk, name, inSignature, indexes, info);
    soup.appPk = inApp.pk;
    mFile.insert(soup);
    session.send(kDSendSoup);
    error = receiveEntries(soup, inPackages, nullptr, ioProgress, end);
    found = soup;
  }

  if (error == 0) {
    // the time of this backup in the soup's info, here and on the Newton
    SoupRecord soup = *found;
    if (ISNIL(info))
      info = SoupInfoFrame(soup);
    SetFrameSlot(info, MakeSymbol("NCKLastBackupTime"), MAKEINT((int64_t)syncTime + 1));
    SetSoupInfo(soup, info);
    mFile.update(soup);
    session.setSoupInfo(info);
    ++mCounts.soups;
  }
  // what came stays, even if it was cancelled
  transaction.commit();
  return error;
}


void Backup::synchronise(SoupRecord & ioSoup, const StoreRecord & inStore, bool inComplete, const BackupIds & inIds)
{
  Session & session = mDock.session();
  // the Newton's entries the file doesn't have: to delete on the Newton,
  // after all soups, if it's clear or the user says so
  std::set<int64_t> local;
  for (const EntryRecord & entry : mFile.entries(ioSoup.pk))
    if (entry.uniqueId < kImportIdBase)
      local.insert(entry.uniqueId);
  DeletionPlan::Soup soup;
  soup.store = inStore.name;
  soup.name = ioSoup.name;
  soup.storePk = inStore.pk;
  soup.onNewton = inIds.ids().size();
  for (int64_t id : inIds.ids())
    if (!local.count(id))
      soup.ids.push_back(id);
  if (!soup.ids.empty()) {
    if (!inComplete) {
      // the file never had all of this soup: what it lacks isn't deleted
      Log(kLogMinimum, "backup: %s: %d entries the file lacks; its backup never finished, none deleted",
          ioSoup.name.c_str(), (int)soup.ids.size());
      mPlan.incomplete.push_back(inStore.name + ": " + ioSoup.name);
    } else {
      mPlan.total += soup.ids.size();
      mPlan.soups.push_back(soup);
    }
  }
  // the entries made on the desktop: to the Newton, with the ids it gives
  for (EntryRecord entry : mFile.entries(ioSoup.pk, true)) {
    if (entry.uniqueId < kImportIdBase)
      continue;
    if (SendImportedEntry(mDock, mFile, ioSoup, entry) == 0)
      ++mCounts.added;
  }
}


void DeletionPlan::assess()
{
  // clear: a few entries, no soup losing much of itself, no incomplete soup
  clear = incomplete.empty() && total <= kClearTotal;
  for (const Soup & soup : soups)
    if (soup.ids.size() * kClearShare > soup.onNewton)
      clear = false;
}


void Backup::deleteOnNewton()
{
  if (mPlan.soups.empty() && mPlan.incomplete.empty())
    return;
  mPlan.assess();
  Deletions decision = Deletions::kOnlyAdd;
  if (mPlan.clear)
    decision = Deletions::kDelete;
  else if (mDecide)
    decision = mDecide(mPlan);
  if (decision != Deletions::kDelete || mPlan.soups.empty()) {
    if (mPlan.total)
      Log(kLogMinimum, "backup: %d entries the file lacks left on the Newton", (int)mPlan.total);
    return;
  }
  Session & session = mDock.session();
  for (const DeletionPlan::Soup & soup : mPlan.soups) {
    std::vector<StoreRecord> stores = mFile.stores();
    auto store = std::find_if(stores.begin(), stores.end(), [&](const StoreRecord & s) { return s.pk == soup.storePk; });
    if (store == stores.end())
      continue;
    session.setCurrentStore(RefVar(StoreFrame(*store)), false);
    if (session.setCurrentSoup(RefVar(MakeStringFromUTF8(soup.name))) != 0)
      continue;
    RefVar ids(MakeArray(0));
    for (int64_t id : soup.ids)
      AddArraySlot(ids, MAKEINT(id));
    Log(kLogMinimum, "backup: %s: deleting %d entries on the Newton", soup.name.c_str(), (int)soup.ids.size());
    session.deleteEntryIdList(ids);
    mCounts.deletedOnNewton += soup.ids.size();
  }
}


void Backup::done(int inError)
{
  // the device's last backup
  if (std::optional<DeviceRecord> device = mFile.device()) {
    device->backupDate = CoreDataFromTime(Now());
    device->backupError = inError == kDockErrDisconnected ? 0 : inError;
    mFile.update(*device);
  }
  if (inError != kDockErrDisconnected && !mDock.newton().newton1)
    mDock.setDesktopControl(false);       // kDOperationDone
}

} // namespace ncx
