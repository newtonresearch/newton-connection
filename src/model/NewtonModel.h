/*
 File: NewtonModel.h

 The model and Newton objects (PORTING.md 4.1): what the Cocoa app's
 NCDocument, NCStore(ref), NCSoup(ref) and NCEntry(ref) did with the frames
 a Newton sends. An entry keeps its NSOF; titles come from the soup's slot
 definitions (the Cocoa app's slot.plist, here SoupColumns). For the dock
 thread: includes newton-framework's headers.
 */

#ifndef NCX_MODEL_NEWTONMODEL_H
#define NCX_MODEL_NEWTONMODEL_H

#include "model/DeviceFile.h"

#include "Objects.h"

#include <string>
#include <vector>

namespace ncx {

/*-- the soups' columns (slot.plist) -----------------------------------------*/

struct SoupColumn
{
  std::string slot;     // the entry's slot
  std::string header;   // the column's title
  std::string tag;      // title, info1, info2
  std::string type;     // string, name, nameRefArray, dateRef, GMT, symbol
};

/** The columns of a soup's entries: the title first, then info1, info2 if
    the soup has them. A soup without its own: [*default*] (the 'tag slot). */
const std::vector<SoupColumn> & SoupColumns(const std::string & inSoupName);
/** Soups whose entries show their folder (labels). */
bool SoupShowsLabels(const std::string & inSoupName);

/** A slot as text, by a column's type: names "Ada Lovelace", dates
    FormatDateTime, GMT "+01:00", symbols their name, strings as they are;
    "" otherwise. */
std::string SlotText(RefArg inSlot, const std::string & inType);

/** An entry's title, as the Cocoa app set ZTITLE. */
std::string EntryTitle(RefArg inEntry, const std::string & inSoupName);

/*-- device, stores, apps ----------------------------------------------------*/

/* The file's device: SetDevice (DeviceList.h). */

/** The store with a store frame's signature, made if there is none, and set
    from the frame (NCDocument addStore:). */
StoreRecord AddStore(DeviceFile & ioFile, int64_t inDevicePk, RefArg inStoreFrame);
/** The store as a frame (NCStore ref). */
Ref StoreFrame(const StoreRecord & inStore);
/** Internal first, then by name (NCDocument stores). */
std::vector<StoreRecord> SortedStores(DeviceFile & ioFile);

/** The app of that name, made if there is none (NCDocument findApp:). */
AppRecord FindOrAddApp(DeviceFile & ioFile, const std::string & inName);

/** NewtonOS 1 has no apps: the app a soup belongs to, as the NewtonOS 2
    apps have them (the Cocoa app's appslot.plist: "Calendar" is Dates'),
    or the soup's own name. */
std::string Newton1AppName(const std::string & inSoupName);

/*-- soups -------------------------------------------------------------------*/

/** A new soup on a store (NCStore addSoup:...): its name, description and
    app name from the info's soupDef. Not in the file yet. */
SoupRecord MakeSoup(int64_t inStorePk, const std::string & inName, int64_t inSignature, RefArg inIndexes, RefArg inInfo);
void SetSoupInfo(SoupRecord & ioSoup, RefArg inInfo);       // and lastBackupTime from NCKLastBackupTime
void SetSoupIndexes(SoupRecord & ioSoup, RefArg inIndexes);
Ref SoupInfoFrame(const SoupRecord & inSoup);               // a frame always
Ref SoupIndexArray(const SoupRecord & inSoup);              // an array always

/*-- entries -----------------------------------------------------------------*/

/** An entry's _uniqueId. Imported entries' ids (kImportIdBase and up) don't
    fit a Newton integer: the Cocoa app (64-bit Refs) kept them anyway, and
    NSOF has only 30 bits of them, so it read them unsigned. MakeUniqueId
    makes the same 30 bits, so NSOF stays as the Cocoa app wrote it. */
int64_t UniqueIdOf(RefArg inId);       // 0 if not an integer
Ref MakeUniqueId(int64_t inId);

/** An entry from the Newton (its frame and its NSOF), added to the soup, or
    the entry with its _uniqueId updated (NCSoup addEntry:withNSOFData:):
    class, title, _uniqueId, _modTime; the soup's lastBackupId follows. */
EntryRecord AddEntry(DeviceFile & ioFile, SoupRecord & ioSoup, RefArg inEntry, const Bytes & inNSOF);

/** An entry made on the desktop (an import): without a _uniqueId it gets the
    next from kImportIdBase on, and a _modTime of now (NCSoup addEntry:). */
EntryRecord AddImportedEntry(DeviceFile & ioFile, SoupRecord & ioSoup, RefArg ioEntry);

/** After the Newton took an entry: its new _uniqueId and _modTime (NCEntry
    update:). */
void UpdateEntry(DeviceFile & ioFile, SoupRecord & ioSoup, EntryRecord & ioEntry, RefArg inAddedEntry);

/** The entry's frame (NIL if its NSOF can't be read). */
Ref EntryFrame(const EntryRecord & inEntry);

} // namespace ncx

#endif // NCX_MODEL_NEWTONMODEL_H
