/*
 File: Restore.cc

 Restoring a Newton from its .newtondevice file (Restore.h).
 */

#include "dock/Restore.h"
#include "dock/Backup.h"
#include "dock/DockController.h"
#include "base/Log.h"
#include "base/NewtonStrings.h"
#include "comms/DockRefs.h"
#include "model/DeviceFile.h"
#include "model/NewtonModel.h"

#include "RSSymbols.h"

namespace ncx {

namespace {

void PutLong(std::vector<uint8_t> & ioBytes, uint32_t inValue)
{
  for (int shift = 24; shift >= 0; shift -= 8)
    ioBytes.push_back((uint8_t)(inValue >> shift));
}

} // namespace


int Restore::answer(Progress * ioProgress)
{
  // the result of what went to the Newton, or its cancel
  Session & session = mDock.session();
  for (;;) {
    DockEvent event = session.receive(kDAnyEvent);
    if (event.command == kDResult || event.command == kDAddedID) {
      if (event.command == kDAddedID)
        event = DockEvent::MakeValue(kDResult, 0);    // NewtonOS 1's adde
      if (event.value() == 0 && ioProgress && ioProgress->cancelled()) {
        session.send(kDOperationCanceled);        // no answer to it (as the Cocoa app)
        return kDockErrAborted;
      }
      return event.value();
    }
    if (event.command == kDOperationCanceled) {
      session.send(kDOpCanceledAck);
      mNewtonCancelled = true;
      return kDockErrAborted;
    }
    Log(kLogMinimum, "restore: '%s' instead of a result, ignored", CmdName(event.command).c_str());
  }
}


int Restore::run(const RestoreSelection & inSelection, Progress * ioProgress)
{
  mCounts = RestoreCounts();
  mNewtonCancelled = false;
  if (mDock.newton().newton1)
    return restoreNewton1(inSelection, ioProgress);
  Session & session = mDock.session();
  mDock.setDesktopControl(true);
  session.send(kDRequestToRestore);
  int error = session.receiveResult();

  if (error == 0) {
    // what the data is: NewtonOS 1.x or 2.x, from what machine
    NewtonInfo info;
    if (std::optional<DeviceRecord> device = mFile.device())
      info = NewtonInfoOf(*device);
    std::vector<uint8_t> source;
    PutLong(source, info.fROMVersion < 0x00020000 ? kOnePointXData : kTwoPointXData);
    PutLong(source, info.fManufacturer);
    PutLong(source, info.fMachineType);
    session.send(DockEvent::MakeData(kDSourceVersion, std::move(source)));
    error = session.receiveResult();
  }
  if (error == 0)
    error = session.setCurrentStore(RefVar(StoreFrame(inSelection.target)), false);

  if (ioProgress) {
    ioProgress->setTotal((int64_t)(inSelection.apps.size() + inSelection.packages.size()));
    ioProgress->setCompleted(0);
  }
  int64_t done = 0;
  for (size_t a = 0; a < inSelection.apps.size() && error == 0; ++a) {
    const AppRecord & app = inSelection.apps[a];
    for (SoupRecord soup : mFile.soups(inSelection.source.pk)) {
      if (soup.appPk != app.pk)
        continue;
      if (ioProgress)
        ioProgress->setDescription(inSelection.target.name + ": " + app.name);
      error = restoreSoup(soup, ioProgress);
      if (error != 0)
        break;
    }
    if (ioProgress)
      ioProgress->setCompleted(++done);
  }
  for (size_t p = 0; p < inSelection.packages.size() && error == 0; ++p) {
    if (ioProgress)
      ioProgress->setDescription(inSelection.target.name + ": " + inSelection.packages[p].title.value_or("a package"));
    error = restorePackage(inSelection.packages[p], ioProgress);
    if (ioProgress)
      ioProgress->setCompleted(++done);
  }

  // cancelled on the Newton: it no longer waits for the end of control
  if (!mNewtonCancelled)
    mDock.setDesktopControl(false);
  return error;
}


int Restore::restoreSoup(SoupRecord & ioSoup, Progress * ioProgress)
{
  Session & session = mDock.session();
  RefVar name(MakeStringFromUTF8(ioSoup.name));
  // the soup afresh: deleted if it's there, made again
  if (session.setCurrentSoup(name) == 0)
    session.deleteSoup();
  int error = session.createSoup(name, RefVar(SoupIndexArray(ioSoup)));
  if (error == 0)
    error = session.setCurrentSoup(name);
  if (error == 0)
    error = session.setSoupInfo(RefVar(SoupInfoFrame(ioSoup)));
  bool newton1 = mDock.newton().newton1;
  if (error == 0 && !newton1) {
    session.sendValue(kDSetSoupSignature, (int32_t)ioSoup.signature);
    error = session.receiveResult();
  }
  if (error != 0)
    return error;

  DeviceFile::Transaction transaction(mFile);
  for (EntryRecord entry : mFile.entries(ioSoup.pk, true)) {
    if (entry.uniqueId >= kImportIdBase) {
      // made on the desktop: the Newton gives it its id
      error = SendImportedEntry(mDock, mFile, ioSoup, entry);
      if (error == kDockErrAborted)
        mNewtonCancelled = true;
      if (error != 0)
        break;
    } else if (newton1) {
      // NewtonOS 1 has no auni: added with its id, answered with the id or
      // a result
      session.send(DockEvent::MakeData(kDAddEntry, entry.refData));
      error = answer(ioProgress);
      if (error != 0)
        break;
    } else {
      session.send(DockEvent::MakeData(kDAddEntryWithUniqueID, entry.refData));
      error = answer(ioProgress);
      if (error != 0)
        break;
    }
    ++mCounts.entries;
  }
  transaction.commit();
  if (error == 0)
    ++mCounts.soups;
  return error;
}


int Restore::restorePackage(const EntryRecord & inPackage, Progress * ioProgress)
{
  Session & session = mDock.session();
  std::optional<EntryRecord> entry = mFile.entry(inPackage.pk, true);
  RefVar frame(entry ? EntryFrame(*entry) : (Ref)NILREF);
  RefVar pkgRef(IsFrame(frame) ? GetFrameSlot(frame, MakeSymbol("pkgRef")) : (Ref)NILREF);
  RefVar name(IsFrame(frame) ? GetFrameSlot(frame, MakeSymbol("packageName")) : (Ref)NILREF);
  if (!IsBinary(pkgRef))
    return 0;                       // not a package (nothing to install)
  // the one there goes first (whatever the result)
  if (IsString(name)) {
    session.sendRef(kDRemovePackage, name);
    session.receiveResult();
  }
  const uint8_t * bytes = (const uint8_t *)BinaryData(pkgRef);
  session.send(DockEvent::MakeData(kDLoadPackage, std::vector<uint8_t>(bytes, bytes + Length(pkgRef))));
  int error = answer(ioProgress);
  if (error == 0)
    ++mCounts.packages;
  return error;
}

} // namespace ncx
