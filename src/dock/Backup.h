/*
 File: Backup.h

 Backing up a Newton into its .newtondevice file (PORTING.md 5.2.4; the
 Cocoa app's NCBackupComponent, and do_GALS of its
 NCDockProtocolController). The desktop asks (kDRequestToSync), or the
 Newton does ('ssyn', Dock's Backup); then, for every store the Newton's
 sync options name, for every app (System first, Packages last and only if
 asked for), for every soup:
   - a soup the file doesn't have: its info, its indexes, all its entries
     (kDSendSoup)
   - a soup it has: its info and indexes if they changed, then the entries
     added or changed since the last backup and the ids of all the others
     (kDBackupSoup, kDSetBaseID, kDBackupIDs); then, as the Cocoa app
     synchronises:
       entries the Newton no longer has are deleted from the file;
       entries the Newton has but the file doesn't are deleted on the
       Newton (the Cocoa app takes them for deleted on the desktop);
       entries made on the desktop (imported) are added on the Newton
   - its info gets the time of this backup (NCKLastBackupTime), in the
     file and on the Newton
 Each soup is a transaction of its own: a backup that breaks keeps what it
 got. In the Packages soup only packages count (entries with a pkgRef).

 On the dock thread.
 */

#ifndef NCX_DOCK_BACKUP_H
#define NCX_DOCK_BACKUP_H

#include "base/Progress.h"
#include "model/Records.h"

#include "Objects.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace ncx {

class DockController;
class DeviceFile;

/** After docking: the Newton in the file (the Cocoa app's do_GALS): its
    name and info, its stores (found by signature, or new), and what
    Gestalt says (OS version, manufacture date, processor). inSetClock: the
    Newton's clock to the desktop's (the SetNewtonTime preference). Takes
    control and gives it back. */
void UpdateDevice(DockController & ioDock, DeviceFile & ioFile, bool inSetClock = false);

/** The ids of a soup's entries, as a Newton sends them while it backs up:
    kDSetBaseID, then kDBackupIDs (big-endian 16-bit codes: n is base + n,
    -n the n ids after the last one; 0x8000 ends them). */
class BackupIds
{
public:
  void setBase(int64_t inBase) { mBase = inBase; }
  void add(int64_t inId) { mIds.insert(inId); }
  /** kDBackupIDs' data. */
  void addCoded(const std::vector<uint8_t> & inData);
  const std::set<int64_t> & ids() const { return mIds; }

private:
  int64_t mBase = 0;
  int64_t mRunBase = 0;
  std::set<int64_t> mIds;
};

struct BackupCounts
{
  size_t soups = 0;               // backed up
  size_t entries = 0;             // that came (new or changed)
  size_t removed = 0;             // from the file: no longer on the Newton
  size_t deletedOnNewton = 0;     // on the Newton: deleted on the desktop
  size_t added = 0;               // to the Newton: imported on the desktop
};

class Backup
{
public:
  Backup(DockController & ioDock, DeviceFile & ioFile) : mDock(ioDock), mFile(ioFile) { }

  /** The desktop asks for it (in control, kDRequestToSync), then backs up.
      0, or the error (kDockErrAborted: cancelled). ioProgress: apps done
      of all; cancel() stops it after the entry in hand. */
  int run(Progress * ioProgress = nullptr);
  /** The Newton asked for it ('ssyn'): the backup itself. */
  int answer(Progress * ioProgress = nullptr);

  const BackupCounts & counts() const { return mCounts; }

private:
  int backup(Progress * ioProgress);
  int backupSoup(const StoreRecord & inStore, const AppRecord & inApp, bool inPackages, RefArg inSoupName,
                 int64_t inSignature, Progress * ioProgress);
  int receiveEntries(SoupRecord & ioSoup, bool inPackages, BackupIds * ioIds, Progress * ioProgress, EventType & outEnd);
  void synchronise(SoupRecord & ioSoup, const BackupIds & inIds);
  void done(int inError);

  DockController & mDock;
  DeviceFile & mFile;
  BackupCounts mCounts;
};

} // namespace ncx

#endif // NCX_DOCK_BACKUP_H
