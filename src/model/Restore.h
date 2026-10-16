/*
 File: Restore.h

 What can be restored where (PORTING.md 4.1; the Cocoa app's NCRestoreInfo
 and NCDocument buildRestoreInfo): for each store of the Newton that is
 connected (Internal first), the backed-up stores of the same kind
 (internal, or a card), each with its apps (but Packages) and its
 packages. Which of them the user picks is the UI's. No Newton objects.
 */

#ifndef NCX_MODEL_RESTORE_H
#define NCX_MODEL_RESTORE_H

#include "model/DeviceFile.h"

#include <vector>

namespace ncx {

struct RestoreFrom
{
  StoreRecord store;                      // backed up, in the file
  std::vector<AppRecord> apps;            // by name, but Packages
  std::vector<EntryRecord> packages;      // by title, without their data
};

struct RestoreChoice
{
  StoreRecord store;                      // on the Newton
  std::vector<RestoreFrom> sources;       // the file's stores of its kind
};

/** The choices for the Newton's stores (its tethered stores, as records:
    from the file, or made from its store frames). */
std::vector<RestoreChoice> RestoreChoices(DeviceFile & ioFile, std::vector<StoreRecord> inNewtonStores);

} // namespace ncx

#endif // NCX_MODEL_RESTORE_H
