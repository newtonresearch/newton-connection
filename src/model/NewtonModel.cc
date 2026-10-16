/*
 File: NewtonModel.cc

 The model and Newton objects (NewtonModel.h).
 */

#include "model/NewtonModel.h"
#include "base/Dates.h"
#include "base/Log.h"
#include "base/NewtonStrings.h"
#include "comms/DockRefs.h"

#include "RSSymbols.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace ncx {

/*-- the soups' columns ------------------------------------------------------*/

namespace {

// slot.plist, the Cocoa app's: soup -> [title, info1, info2]
const std::map<std::string, std::vector<SoupColumn>> & ColumnTable()
{
  static const std::map<std::string, std::vector<SoupColumn>> table = {
    { "Names",            { { "name", "Name", "title", "name" }, { "city", "Location", "info1", "string" } } },
    { "LinkSetups",       { { "setupName", "Title", "title", "string" } } },
    { "Calls",            { { "name", "Name", "title", "string" }, { "phoneNumber", "Number", "info1", "string" },
                            { "callDate", "Time", "info2", "dateRef" } } },
    { "Packages",         { { "packageName", "Title", "title", "string" } } },
    { "Cities",           { { "name", "Title", "title", "string" }, { "gmt", "GMT", "info1", "GMT" },
                            { "areaCode", "Area code", "info2", "string" } } },
    { "System",           { { "tag", "Title", "title", "string" } } },
    { "OutBox",           { { "title", "Title", "title", "string" }, { "toRef", "To", "info1", "nameRefArray" },
                            { "timestamp", "Date", "info2", "dateRef" } } },
    { "InBox",            { { "title", "Title", "title", "string" }, { "fromName", "From", "info1", "string" },
                            { "timestamp", "Date", "info2", "dateRef" } } },
    { "Repeat Notes",     { { "mtgText", "Title", "title", "string" }, { "mtgStartDate", "Time", "info1", "dateRef" } } },
    { "Calendar",         { { "mtgText", "Title", "title", "string" }, { "mtgStartDate", "Time", "info1", "dateRef" } } },
    { "Repeat Meetings",  { { "mtgText", "Title", "title", "string" }, { "mtgStartDate", "Time", "info1", "dateRef" } } },
    { "Calendar Notes",   { { "mtgText", "Title", "title", "string" }, { "mtgStartDate", "Time", "info1", "dateRef" } } },
    { "DNS Cache:Newton", { { "targetDomainName", "Title", "title", "string" } } },
    { "SystemAlarmSoup",  { { "key", "Title", "title", "string" }, { "time", "Time", "info1", "dateRef" } } },
    { "Internet Setups",  { { "setupName", "Title", "title", "string" } } },
    { "Directory",        { { "app", "Title", "title", "symbol" } } },
  };
  return table;
}

} // namespace


const std::vector<SoupColumn> & SoupColumns(const std::string & inSoupName)
{
  static const std::vector<SoupColumn> defaultColumns = { { "tag", "Title", "title", "string" } };   // *default*
  auto found = ColumnTable().find(inSoupName);
  return found != ColumnTable().end() ? found->second : defaultColumns;
}


bool SoupShowsLabels(const std::string & inSoupName)
{
  // *show-labels*
  return inSoupName == "InBox" || inSoupName == "OutBox" || inSoupName == "NewtWorks" || inSoupName == "Notes"
      || inSoupName == "Names";
}


namespace {

std::string NameText(RefArg inName)
{
  // honorific, first, last: those that are there
  std::string text;
  for (RefVar part : { GetFrameSlot(inName, MakeSymbol("honorific")), GetFrameSlot(inName, SYMA(first)),
                       GetFrameSlot(inName, SYMA(last)) })
    if (IsString(part)) {
      if (!text.empty())
        text += ' ';
      text += UTF8FromString(part);
    }
  return text;
}

} // namespace


std::string SlotText(RefArg inSlot, const std::string & inType)
{
  if (inType == "name")
    return IsFrame(inSlot) ? NameText(inSlot) : std::string();
  if (inType == "nameRefArray") {
    if (IsArray(inSlot) && Length(inSlot) > 0) {
      RefVar first(GetArraySlot(inSlot, 0));
      return IsFrame(first) ? NameText(GetFrameSlot(first, SYMA(name))) : std::string();
    }
    return std::string();
  }
  if (inType == "dateRef" || inType == "date")
    return ISINT(inSlot) ? FormatDateTime(TimeFromNewton(RINT(inSlot))) : std::string();
  if (inType == "GMT") {
    if (!ISINT(inSlot))
      return std::string();
    int minutes = (int)RINT(inSlot) / 60;
    if (minutes == 0)
      return std::string();
    char text[16];
    std::snprintf(text, sizeof(text), "%c%02d:%02d", minutes > 0 ? '+' : '-', std::abs(minutes) / 60, std::abs(minutes) % 60);
    return text;
  }
  if (inType == "symbol")
    return UTF8FromSymbol(inSlot);
  return IsString(inSlot) ? UTF8FromString(inSlot) : std::string();
}


std::string EntryTitle(RefArg inEntry, const std::string & inSoupName)
{
  // the soup's title slot; without one 'title, or 'name if there is no title
  auto found = ColumnTable().find(inSoupName);
  std::string slot = "title", type = "string";
  if (found != ColumnTable().end()) {
    slot = found->second[0].slot;
    type = found->second[0].type;
  } else if (ISNIL(GetFrameSlot(inEntry, SYMA(title))) && NOTNIL(GetFrameSlot(inEntry, SYMA(name)))) {
    slot = "name";
  }
  // a Names entry may be a company or a worksite
  if (type == "name") {
    RefVar company(MakeSymbol("company")), worksite(MakeSymbol("worksite"));
    if (EQRef(ClassOf(inEntry), company) && NOTNIL(GetFrameSlot(inEntry, company))) {
      slot = "company";
      type = "string";
    } else if (EQRef(ClassOf(inEntry), worksite) && NOTNIL(GetFrameSlot(inEntry, MakeSymbol("place")))) {
      slot = "place";
      type = "string";
    }
  }
  RefVar title(GetFrameSlot(inEntry, MakeSymbol(slot.c_str())));
  return NOTNIL(title) ? SlotText(title, type) : std::string();
}


/*-- device, stores, apps ----------------------------------------------------*/

StoreRecord AddStore(DeviceFile & ioFile, int64_t inDevicePk, RefArg inStoreFrame)
{
  RefVar sigRef(GetFrameSlot(inStoreFrame, MakeSymbol("signature")));
  int64_t signature = ISINT(sigRef) ? RINT(sigRef) : 0;
  StoreRecord store;
  bool known = false;
  for (const StoreRecord & s : ioFile.stores())
    if (s.signature == signature) {
      store = s;
      known = true;
      break;
    }
  store.devicePk = inDevicePk;
  store.name = UTF8FromString(GetFrameSlot(inStoreFrame, SYMA(name)));
  store.kind = UTF8FromString(GetFrameSlot(inStoreFrame, SYMA(kind)));
  store.signature = signature;
  RefVar item(GetFrameSlot(inStoreFrame, MakeSymbol("totalSize")));
  store.totalSize = ISINT(item) ? RINT(item) : 0;
  item = GetFrameSlot(inStoreFrame, MakeSymbol("usedSize"));
  store.usedSize = ISINT(item) ? RINT(item) : 0;
  // a NewtonOS 1 store frame may have no more than these
  store.readOnly = NOTNIL(GetFrameSlot(inStoreFrame, MakeSymbol("readOnly")));
  store.defaultStore = NOTNIL(GetFrameSlot(inStoreFrame, MakeSymbol("defaultStore")));
  item = GetFrameSlot(inStoreFrame, MakeSymbol("storeVersion"));
  if (ISINT(item))
    store.storeVersion = RINT(item);
  item = GetFrameSlot(inStoreFrame, MakeSymbol("storePassword"));
  if (IsString(item))
    store.password = UTF8FromString(item);
  if (known)
    ioFile.update(store);
  else
    ioFile.insert(store);
  return store;
}


Ref StoreFrame(const StoreRecord & inStore)
{
  RefVar frame(AllocateFrame());
  SetFrameSlot(frame, SYMA(name), MakeStringFromUTF8(inStore.name));
  SetFrameSlot(frame, SYMA(kind), MakeStringFromUTF8(inStore.kind));
  SetFrameSlot(frame, MakeSymbol("signature"), MAKEINT(inStore.signature));
  SetFrameSlot(frame, MakeSymbol("totalSize"), MAKEINT(inStore.totalSize));
  SetFrameSlot(frame, MakeSymbol("usedSize"), MAKEINT(inStore.usedSize));
  SetFrameSlot(frame, MakeSymbol("readOnly"), MAKEBOOLEAN(inStore.readOnly));
  SetFrameSlot(frame, MakeSymbol("defaultStore"), MAKEBOOLEAN(inStore.defaultStore));
  SetFrameSlot(frame, MakeSymbol("storePassword"), MakeStringFromUTF8(inStore.password ? *inStore.password : std::string()));
  SetFrameSlot(frame, MakeSymbol("storeVersion"), MAKEINT(inStore.storeVersion));
  return frame;
}


std::vector<StoreRecord> SortedStores(DeviceFile & ioFile)
{
  std::vector<StoreRecord> stores = ioFile.stores();
  SortStores(stores);
  return stores;
}


AppRecord FindOrAddApp(DeviceFile & ioFile, const std::string & inName)
{
  if (std::optional<AppRecord> app = ioFile.findApp(inName))
    return *app;
  AppRecord app;
  app.name = inName;
  ioFile.insert(app);
  return app;
}


/*-- soups -------------------------------------------------------------------*/

SoupRecord MakeSoup(int64_t inStorePk, const std::string & inName, int64_t inSignature, RefArg inIndexes, RefArg inInfo)
{
  SoupRecord soup;
  soup.storePk = inStorePk;
  soup.name = inName;
  soup.signature = inSignature;
  soup.lastBackupId = 0;
  soup.lastImportId = kImportIdBase;
  SetSoupIndexes(soup, inIndexes);
  SetSoupInfo(soup, inInfo);
  // without a soupDef, or one not fully slotted
  soup.descr = "No soup definition.";
  soup.appName = "--";
  RefVar soupDef;
  if (IsFrame(inInfo) && IsFrame(soupDef = GetFrameSlot(inInfo, MakeSymbol("soupDef")))) {
    RefVar item(GetFrameSlot(soupDef, SYMA(name)));
    if (IsString(item))
      soup.name = UTF8FromString(item);
    if (IsString(item = GetFrameSlot(soupDef, MakeSymbol("userDescr"))))
      soup.descr = UTF8FromString(item);
    if (IsString(item = GetFrameSlot(soupDef, MakeSymbol("ownerAppName"))))
      soup.appName = UTF8FromString(item);
    else if (IsSymbol(item = GetFrameSlot(soupDef, MakeSymbol("ownerApp"))))
      soup.appName = UTF8FromSymbol(item);
  }
  return soup;
}


void SetSoupInfo(SoupRecord & ioSoup, RefArg inInfo)
{
  ioSoup.info = FlattenToBytes(inInfo);
  RefVar time(IsFrame(inInfo) ? GetFrameSlot(inInfo, MakeSymbol("NCKLastBackupTime")) : (Ref)NILREF);
  ioSoup.lastBackupTime = ISINT(time) ? RINT(time) : 0;
}


void SetSoupIndexes(SoupRecord & ioSoup, RefArg inIndexes)
{
  ioSoup.indexes = FlattenToBytes(inIndexes);
}


Ref SoupInfoFrame(const SoupRecord & inSoup)
{
  RefVar info(UnflattenBytes(inSoup.info));
  return IsFrame(info) ? (Ref)info : AllocateFrame();
}


Ref SoupIndexArray(const SoupRecord & inSoup)
{
  RefVar indexes(UnflattenBytes(inSoup.indexes));
  return IsArray(indexes) ? (Ref)indexes : MakeArray(0);
}


/*-- entries -----------------------------------------------------------------*/

int64_t UniqueIdOf(RefArg inId)
{
  return ISINT(inId) ? (int64_t)((uint64_t)RINT(inId) & 0x3FFFFFFF) : 0;
}


Ref MakeUniqueId(int64_t inId)
{
  // the low 30 bits, sign-extended: a Newton integer
  int64_t value = (int64_t)(inId & 0x3FFFFFFF);
  if (value & 0x20000000)
    value -= 0x40000000;
  return MAKEINT(value);
}


EntryRecord AddEntry(DeviceFile & ioFile, SoupRecord & ioSoup, RefArg inEntry, const Bytes & inNSOF)
{
  int64_t uniqueId = UniqueIdOf(GetFrameSlot(inEntry, SYMA(_uniqueId)));
  std::optional<EntryRecord> found = ioFile.findEntry(ioSoup.pk, uniqueId, false);
  EntryRecord entry = found ? *found : EntryRecord();
  entry.soupPk = ioSoup.pk;
  entry.refData = inNSOF;
  RefVar entryClass(GetFrameSlot(inEntry, SYMA(class)));
  if (IsSymbol(entryClass))
    entry.refClass = UTF8FromSymbol(entryClass);
  else if (FrameHasSlot(inEntry, MakeSymbol("pkgRef")))
    entry.refClass = "*package*";
  entry.title = EntryTitle(inEntry, ioSoup.name);
  // the highest _uniqueId, for the next (incremental) backup
  if (uniqueId < kImportIdBase && ioSoup.lastBackupId < uniqueId) {
    ioSoup.lastBackupId = uniqueId;
    ioFile.update(ioSoup);
  }
  entry.uniqueId = uniqueId;
  Ref modTime = GetFrameSlot(inEntry, SYMA(_modTime));
  entry.modTime = CoreDataFromTime(ISINT(modTime) ? TimeFromNewton(RINT(modTime)) : Now());
  if (found)
    ioFile.update(entry);
  else
    ioFile.insert(entry);
  return entry;
}


EntryRecord AddImportedEntry(DeviceFile & ioFile, SoupRecord & ioSoup, RefArg ioEntry)
{
  if (!ISINT(GetFrameSlot(ioEntry, SYMA(_uniqueId)))) {
    // made here, in no soup yet: a temporary id, until it is synced
    int64_t id = (ioSoup.lastImportId ? *ioSoup.lastImportId : kImportIdBase) + 1;
    ioSoup.lastImportId = id;
    ioFile.update(ioSoup);
    SetFrameSlot(ioEntry, SYMA(_uniqueId), MakeUniqueId(id));
    SetFrameSlot(ioEntry, SYMA(_modTime), MAKEINT(NewtonFromTime(Now())));
  }
  return AddEntry(ioFile, ioSoup, ioEntry, FlattenToBytes(ioEntry));
}


void UpdateEntry(DeviceFile & ioFile, SoupRecord & ioSoup, EntryRecord & ioEntry, RefArg inAddedEntry)
{
  ioEntry.refData = FlattenToBytes(inAddedEntry);
  int64_t uniqueId = UniqueIdOf(GetFrameSlot(inAddedEntry, SYMA(_uniqueId)));
  if (ioSoup.lastBackupId < uniqueId) {
    ioSoup.lastBackupId = uniqueId;
    ioFile.update(ioSoup);
  }
  ioEntry.uniqueId = uniqueId;
  Ref modTime = GetFrameSlot(inAddedEntry, SYMA(_modTime));
  if (ISINT(modTime))
    ioEntry.modTime = CoreDataFromTime(TimeFromNewton(RINT(modTime)));
  ioFile.update(ioEntry);
}


Ref EntryFrame(const EntryRecord & inEntry)
{
  return UnflattenBytes(inEntry.refData);
}

} // namespace ncx
