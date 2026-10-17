/*
 File: SoupEditor.h

 Changing the docked Newton's soups from the desktop (PORTING.md 5.2.6; the
 Cocoa app's do_IMPT, do_DIDL and do_DPKL of its NCDockProtocolController,
 and the delete: of its SoupViewController):
   - entries made on the desktop (imported: AddImportedEntry gives them ids
     from kImportIdBase on) go to the Newton (kDAddEntry) and take the ids
     it gives
   - entries are deleted on the Newton (kDDeleteEntries) and from the file;
     in the Packages soup the packages are removed (kDRemovePackage, by
     name), as the Newton does it
 Without a Newton the UI changes only the file: the next backup sends the
 entries made on the desktop and deletes on the Newton what the file no
 longer has (asking if it isn't clear: DeletionPlan). Packages are removed
 only with the Newton docked (a backup doesn't remove them), as in the Cocoa
 app.

 Importing a file through a translator (on the desktop, or the Newton's
 Import: 'impt', 'tran') comes with the translators (Phase 6); it makes
 entries on the desktop and sends them with add().

 On the dock thread.
 */

#ifndef NCX_DOCK_SOUPEDITOR_H
#define NCX_DOCK_SOUPEDITOR_H

#include <cstdint>
#include <vector>

namespace ncx {

class DockController;
class DeviceFile;

struct SoupEditCounts
{
  size_t added = 0;               // to the Newton (made on the desktop)
  size_t deleted = 0;             // entries, on the Newton and from the file
  size_t packages = 0;            // removed from the Newton and the file
  size_t fileOnly = 0;            // made on the desktop, never sent: from the file only
};

class SoupEditor
{
public:
  SoupEditor(DockController & ioDock, DeviceFile & ioFile) : mDock(ioDock), mFile(ioFile) { }

  /** The soup's entries made on the desktop to the Newton, with the ids it
      gives them, in the file too: inEntryPks (others are left), or all of
      them. Takes control and gives it back. 0, or the Newton's error (what
      went stays). */
  int add(int64_t inSoupPk, const std::vector<int64_t> & inEntryPks = {});

  /** Delete the soup's entries inEntryPks on the Newton and from the file;
      packages are removed by name. Entries made on the desktop that never
      went to the Newton only leave the file. Takes control and gives it
      back. 0, or the Newton's error: what it refused stays in the file. */
  int remove(int64_t inSoupPk, const std::vector<int64_t> & inEntryPks);

  const SoupEditCounts & counts() const { return mCounts; }

private:
  int selectSoup(int64_t inSoupPk);

  DockController & mDock;
  DeviceFile & mFile;
  SoupEditCounts mCounts;
};

} // namespace ncx

#endif // NCX_DOCK_SOUPEDITOR_H
