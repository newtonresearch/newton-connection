/*
 File: Restore.h

 Restoring a Newton from its .newtondevice file (PORTING.md 5.2.5; the
 Cocoa app's NCRestoreComponent, its desktop half). What to restore comes
 from model/Restore.h's choices (the UI's, or ncx-cli's): a store of the
 Newton, a backed-up store of the same kind, its apps, its packages. Then:
 the desktop takes control and asks to restore (rrst), says what the data is
 (sver: NewtonOS 1.x or 2.x, the manufacturer and the machine), and for
 every app, for every soup of it on the backed-up store, the soup on the
 Newton is deleted, made again (its indexes, info, signature), and filled
 with the backed-up entries, each with its _uniqueId (auni); entries made
 on the desktop (imported, kImportIdBase and up) are added (adde) and take
 the id the Newton gives them. The packages are removed and installed again.

 It replaces what is on the Newton: the UI asks first. On the dock thread.
 */

#ifndef NCX_DOCK_RESTORE_H
#define NCX_DOCK_RESTORE_H

#include "base/Progress.h"
#include "model/Records.h"

#include <vector>

namespace ncx {

class DockController;
class DeviceFile;

struct RestoreSelection
{
  StoreRecord target;                     // the Newton's store
  StoreRecord source;                     // the backed-up one
  std::vector<AppRecord> apps;            // their soups on source
  std::vector<EntryRecord> packages;      // source's (model/Restore.h)
};

struct RestoreCounts
{
  size_t soups = 0;
  size_t entries = 0;
  size_t packages = 0;
};

class Restore
{
public:
  Restore(DockController & ioDock, DeviceFile & ioFile) : mDock(ioDock), mFile(ioFile) { }

  /** Restore it. 0, or the error (kDockErrAborted: cancelled here or on
      the Newton). ioProgress: apps and packages done of all; cancel()
      stops it after the entry in hand. */
  int run(const RestoreSelection & inSelection, Progress * ioProgress = nullptr);

  const RestoreCounts & counts() const { return mCounts; }

private:
  int restoreSoup(SoupRecord & ioSoup, Progress * ioProgress);
  int restorePackage(const EntryRecord & inPackage, Progress * ioProgress);
  int answer(Progress * ioProgress);

  DockController & mDock;
  DeviceFile & mFile;
  RestoreCounts mCounts;
  bool mNewtonCancelled = false;
};

} // namespace ncx

#endif // NCX_DOCK_RESTORE_H
