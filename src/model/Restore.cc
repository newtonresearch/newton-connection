/*
 File: Restore.cc

 What can be restored where (Restore.h).
 */

#include "model/Restore.h"

namespace ncx {

std::vector<RestoreChoice> RestoreChoices(DeviceFile & ioFile, std::vector<StoreRecord> inNewtonStores)
{
  SortStores(inNewtonStores);
  std::vector<StoreRecord> backedUp = ioFile.stores();
  SortStores(backedUp);
  std::vector<RestoreChoice> targets;
  for (const StoreRecord & newtonStore : inNewtonStores) {
    RestoreChoice target;
    target.store = newtonStore;
    for (const StoreRecord & store : backedUp)
      if (IsInternal(store) == IsInternal(newtonStore))
        target.sources.push_back({ store, ioFile.storeApps(store.pk, false), ioFile.storePackages(store.pk) });
    targets.push_back(target);
  }
  return targets;
}

} // namespace ncx
