/*
 File: model_test.cc

 Unit tests of NCX's model (PORTING.md Phase 4): binary plists, the
 .newtondevice file and its Core Data bookkeeping, entries and their titles,
 Newton Connection 1 backups, the device list. One section per ctest:

   ncx_model_test <section>    plist, file, delete, entries, titles, backup,
                               devices, large, restore, fixture <file>, or
                               all [<fixture file>]
   ncx_model_test write-backup <file>   a test backup (.nbku), for trying

 Prints "ok <what>" or "FAIL <what>" per check; exit 0 if all are ok. Files
 go to a folder in the temp folder, removed afterwards.
 */

#include "base/Core.h"
#include "base/Dates.h"
#include "base/NewtonStrings.h"
#include "comms/DockRefs.h"
#include "model/BackupImport.h"
#include "model/CoreDataModel.h"
#include "model/DeviceFile.h"
#include "model/DeviceList.h"
#include "model/NewtonModel.h"
#include "model/Plist.h"
#include "model/Restore.h"
#include "platform/AppFolders.h"

#include "RSSymbols.h"
#include "sqlite3.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ncx;

namespace {

int gFailed = 0;

void Check(bool inOk, const std::string & inWhat)
{
  std::printf("%s %s\n", inOk ? "ok     " : "FAIL   ", inWhat.c_str());
  if (!inOk)
    ++gFailed;
}

/** A folder of its own in the temp folder, removed when done. */
struct TempFolder
{
  fs::path path;
  explicit TempFolder(const std::string & inName)
  {
    path = fs::temp_directory_path() / ("ncx-model-" + inName + "-" + std::to_string(Now() % 100000));
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~TempFolder()
  {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

/** One number out of a file, by SQL (a connection of its own). */
int64_t QueryInt(const fs::path & inFile, const std::string & inSQL)
{
  sqlite3 * db = nullptr;
  int64_t value = -1;
  if (sqlite3_open_v2(UTF8FromPath(inFile).c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
    sqlite3_stmt * stmt = nullptr;
    if (sqlite3_prepare_v2(db, inSQL.c_str(), -1, &stmt, nullptr) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW)
      value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
  return value;
}

std::string QueryText(const fs::path & inFile, const std::string & inSQL)
{
  sqlite3 * db = nullptr;
  std::string value;
  if (sqlite3_open_v2(UTF8FromPath(inFile).c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
    sqlite3_stmt * stmt = nullptr;
    if (sqlite3_prepare_v2(db, inSQL.c_str(), -1, &stmt, nullptr) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW
        && sqlite3_column_text(stmt, 0))
      value = (const char *)sqlite3_column_text(stmt, 0);
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
  return value;
}

/** Z_PRIMARYKEY.Z_MAX of every entity is its table's highest Z_PK. */
bool PrimaryKeysAgree(const fs::path & inFile)
{
  static const char * const tables[][2] = { { "Device", "ZDEVICE" }, { "UserInfo", "ZUSERINFO" }, { "Store", "ZSTORE" },
                                            { "App", "ZAPP" }, { "Soup", "ZSOUP" }, { "Entry", "ZENTRY" } };
  for (auto & t : tables) {
    int64_t max = QueryInt(inFile, std::string("SELECT Z_MAX FROM Z_PRIMARYKEY WHERE Z_NAME = '") + t[0] + "'");
    int64_t highest = QueryInt(inFile, std::string("SELECT IFNULL(MAX(Z_PK), 0) FROM ") + t[1]);
    if (max < highest) {
      std::printf("        %s: Z_MAX %lld, highest Z_PK %lld\n", t[0], (long long)max, (long long)highest);
      return false;
    }
  }
  return true;
}

NewtonInfo TestInfo(uint32_t inSerialHigh, uint32_t inSerialLow)
{
  NewtonInfo info;
  info.fNewtonID = 123456;
  info.fManufacturer = 0x01000000;
  info.fMachineType = 0x10003000;
  info.fROMVersion = 0x00020001;
  info.fRAMSize = 4 * 1024 * 1024;
  info.fScreenWidth = 320;
  info.fScreenHeight = 480;
  info.fNOSVersion = 2;
  info.fSerialNumber[0] = inSerialHigh;
  info.fSerialNumber[1] = inSerialLow;
  return info;
}

/*-- Newton objects --*/

Ref Sym(const char * inName) { return MakeSymbol(inName); }

Ref NameFrame(const char * inFirst, const char * inLast)
{
  RefVar name(AllocateFrame());
  SetFrameSlot(name, SYMA(first), MakeStringFromUTF8(inFirst));
  SetFrameSlot(name, SYMA(last), MakeStringFromUTF8(inLast));
  return name;
}

Ref Person(int inId, const char * inFirst, const char * inLast)
{
  RefVar entry(AllocateFrame());
  SetFrameSlot(entry, SYMA(class), Sym("person"));
  SetFrameSlot(entry, SYMA(name), NameFrame(inFirst, inLast));
  SetFrameSlot(entry, Sym("city"), MakeStringFromUTF8("London"));
  SetFrameSlot(entry, SYMA(_uniqueId), MAKEINT(inId));
  SetFrameSlot(entry, SYMA(_modTime), MAKEINT(50000000 + inId));
  return entry;
}

Ref StoreFrameOf(const char * inName, int inSignature)
{
  RefVar store(AllocateFrame());
  SetFrameSlot(store, SYMA(name), MakeStringFromUTF8(inName));
  SetFrameSlot(store, SYMA(kind), MakeStringFromUTF8(std::string(inName) == "Internal" ? "Internal" : "Flash"));
  SetFrameSlot(store, Sym("signature"), MAKEINT(inSignature));
  SetFrameSlot(store, Sym("totalSize"), MAKEINT(4000000));
  SetFrameSlot(store, Sym("usedSize"), MAKEINT(1000000));
  SetFrameSlot(store, Sym("readOnly"), NILREF);
  SetFrameSlot(store, Sym("defaultStore"), TRUEREF);
  SetFrameSlot(store, Sym("storeVersion"), MAKEINT(1));
  return store;
}

Ref SoupInfo(const char * inName, const char * inDescr, const char * inOwner)
{
  RefVar soupDef(AllocateFrame());
  SetFrameSlot(soupDef, SYMA(name), MakeStringFromUTF8(inName));
  SetFrameSlot(soupDef, Sym("userDescr"), MakeStringFromUTF8(inDescr));
  SetFrameSlot(soupDef, Sym("ownerAppName"), MakeStringFromUTF8(inOwner));
  RefVar info(AllocateFrame());
  SetFrameSlot(info, Sym("soupDef"), soupDef);
  SetFrameSlot(info, Sym("NCKLastBackupTime"), MAKEINT(51000000));
  return info;
}

Ref StringArray(std::initializer_list<const char *> inItems)
{
  RefVar array(MakeArray(0));
  for (const char * item : inItems)
    AddArraySlot(array, MakeStringFromUTF8(item));
  return array;
}

/*-- plist -------------------------------------------------------------------*/

void TestPlist()
{
  Plist dict = Plist::Dict();
  dict.set("NSStoreType", Plist::String("SQLite"));
  dict.set("Int", Plist::Int(-5));
  dict.set("Big", Plist::Int(1LL << 40));
  dict.set("Real", Plist::Real(2.5));
  dict.set("Yes", Plist::Bool(true));
  dict.set("Date", Plist::Date(123456.0));
  dict.set("Data", Plist::Data({ 0, 1, 2, 255 }));
  dict.set("Unicode", Plist::String("Gr\xC3\xBC\xC3\x9F" "e"));
  Plist array = Plist::Array({ Plist::Int(1), Plist::String("two") });
  for (int i = 0; i < 300; ++i)       // more than 255 objects: 2-byte refs
    array.append(Plist::Int(i));
  dict.set("Array", array);
  std::vector<uint8_t> bytes = dict.write();
  Check(bytes.size() > 40 && std::memcmp(bytes.data(), "bplist00", 8) == 0, "plist: written as bplist00");
  Plist back;
  std::string error;
  Check(Plist::Read(bytes, back, error) && back == dict, "plist: read back the same " + error);
  const Plist * found = back.find("Unicode");
  Check(found && found->asString() == "Gr\xC3\xBC\xC3\x9F" "e", "plist: a non-ASCII string");
  found = back.find("Big");
  Check(found && found->asInt() == (1LL << 40), "plist: a 64-bit int");
  Check(!Plist::Read({ 'b', 'p', 'l', 'i', 's', 't' }, back, error) && !error.empty(), "plist: not a plist");
  Plist ab = Plist::Dict(), ba = Plist::Dict();
  ab.set("a", Plist::Int(1));
  ab.set("b", Plist::Int(2));
  ba.set("b", Plist::Int(2));
  ba.set("a", Plist::Int(1));
  Check(ab == ba && !(ab == Plist::Dict()), "plist: dictionaries' keys in any order");

  Plist metadata = coredata::MakeMetadata("0000-0000-0000-0001", "Ada");
  Check(coredata::IsThisModel(metadata), "plist: the metadata is the model's");
  Plist read;
  Check(Plist::Read(metadata.write(), read, error) && coredata::IsThisModel(read), "plist: the metadata read back");
  found = read.find("NewtonName");
  Check(found && found->asString() == "Ada", "plist: NewtonName in the metadata");
  Plist other = metadata;
  other.set("NSStoreModelVersionHashes", Plist::Dict());
  Check(!coredata::IsThisModel(other), "plist: another model is not");
  std::string uuid = coredata::MakeUUID();
  Check(uuid.size() == 36 && uuid[8] == '-' && uuid != coredata::MakeUUID(), "plist: a UUID " + uuid);
}

/*-- file --------------------------------------------------------------------*/

void TestFile()
{
  TempFolder folder("file");
  fs::path path = folder.path / PathFromUTF8("Gr\xC3\xBC\xC3\x9F" "e.newtondevice");
  std::string error;
  {
    std::unique_ptr<DeviceFile> file = DeviceFile::Create(path, "0000-0000-0000-0001", "Ada", error);
    Check(file != nullptr, "file: create " + error);
    if (!file)
      return;
    Check(!DeviceFile::Create(path, "0000-0000-0000-0001", "Ada", error), "file: not created twice");
    DeviceFile::Transaction transaction(*file);
    DeviceRecord device = SetDevice(*file, "Ada Lovelace", TestInfo(0x4E65, 0x7774));
    StoreRecord store = AddStore(*file, device.pk, RefVar(StoreFrameOf("Internal", 1111)));
    AppRecord app = FindOrAddApp(*file, "Names");
    SoupRecord soup = MakeSoup(store.pk, "Names", 2222, RefVar(MakeArray(0)), RefVar(SoupInfo("Names", "Name cards", "Names")));
    soup.appPk = app.pk;
    file->insert(soup);
    for (int id = 1; id <= 3; ++id) {
      RefVar entry(Person(id, "Ada", id == 2 ? "Byron" : "Lovelace"));
      AddEntry(*file, soup, entry, FlattenToBytes(entry));
    }
    transaction.commit();
  }
  Check(fs::exists(path), "file: there");
  Check(QueryText(path, "PRAGMA journal_mode") == "wal", "file: WAL journal");
  Check(QueryInt(path, "PRAGMA auto_vacuum") == 2, "file: incremental auto_vacuum");
  Check(PrimaryKeysAgree(path), "file: Z_PRIMARYKEY.Z_MAX follows the rows");
  Check(QueryInt(path, "SELECT COUNT(*) FROM ZENTRY WHERE Z_ENT = 3 AND Z_OPT = 1") == 3, "file: entries' Z_ENT and Z_OPT");
  Check(QueryInt(path, "SELECT COUNT(*) FROM ZDEVICE WHERE ZUSER = (SELECT Z_PK FROM ZUSERINFO WHERE ZDEVICE = ZDEVICE.Z_PK)") == 1,
        "file: the device and its UserInfo");

  error.clear();
  {
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadOnly, error);
    Check(file != nullptr, "file: open read only " + error);
    if (!file)
      return;
    Check(file->newtonId() == "0000-0000-0000-0001" && file->newtonName() == "Ada", "file: NewtonId and NewtonName");
    std::optional<DeviceRecord> device = file->device();
    Check(device && device->name == "Ada Lovelace" && NewtonInfoOf(*device).fMachineType == 0x10003000
          && DeviceId(NewtonInfoOf(*device)) == "0000-4E65-0000-7774", "file: the device");
    std::vector<StoreRecord> stores = file->stores();
    Check(stores.size() == 1 && stores[0].name == "Internal" && stores[0].signature == 1111 && stores[0].defaultStore
          && stores[0].totalSize == 4000000, "file: the store");
    std::vector<SoupRecord> soups = file->soups(stores.empty() ? 0 : stores[0].pk);
    Check(soups.size() == 1 && soups[0].descr == "Name cards" && soups[0].appName == "Names" && soups[0].lastBackupId == 3
          && soups[0].lastBackupTime == 51000000 && soups[0].lastImportId == kImportIdBase, "file: the soup");
    if (soups.empty())
      return;
    std::vector<EntryRecord> entries = file->entries(soups[0].pk);
    Check(entries.size() == 3 && entries[0].refData.empty() && entries[1].title == "Ada Byron"
          && entries[1].refClass == "person", "file: the entries, without their data");
    std::optional<EntryRecord> entry = file->findEntry(soups[0].pk, 3);
    RefVar frame(entry ? EntryFrame(*entry) : (Ref)NILREF);
    Check(IsFrame(frame) && RINT(GetFrameSlot(frame, SYMA(_uniqueId))) == 3, "file: an entry's frame");
    Check(entry && TimeFromCoreData(entry->modTime) == TimeFromNewton(50000003), "file: an entry's modTime");
    bool threw = false;
    try {
      file->deleteEntry(entries[0].pk);
    } catch (const ModelError &) {
      threw = true;
    }
    Check(threw, "file: read only can't be written");
  }

  {
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadWrite, error);
    Check(file != nullptr, "file: open for writing " + error);
    if (!file)
      return;
    std::optional<StoreRecord> store = file->stores().at(0);
    store->usedSize = 2000000;
    file->update(*store);
    file->update(*store);
    file->setIdentity("0000-0000-0000-0001", "Ada L.");
    {
      DeviceFile::Transaction transaction(*file);
      AppRecord app;
      app.name = "Rolled back";
      file->insert(app);
    }   // not committed
    Check(!file->findApp("Rolled back"), "file: a transaction rolled back");
  }
  Check(QueryInt(path, "SELECT Z_OPT FROM ZSTORE") == 3 && QueryInt(path, "SELECT ZUSEDSIZE FROM ZSTORE") == 2000000,
        "file: Z_OPT counts the changes");
  {
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadOnly, error);
    Check(file && file->newtonName() == "Ada L." && coredata::IsThisModel(file->metadata()), "file: the new NewtonName kept");
  }

  // not device files
  fs::path junk = folder.path / "junk.newtondevice";
  {
    std::ofstream out(junk, std::ios::binary);
    out << "not SQLite at all, but long enough to look like a file header of some kind";
  }
  Check(!DeviceFile::Open(junk, DeviceFile::kReadOnly, error) && !error.empty(), "file: junk isn't opened: " + error);
  fs::path other = folder.path / "other.sqlite";
  {
    sqlite3 * db = nullptr;
    sqlite3_open(UTF8FromPath(other).c_str(), &db);
    sqlite3_exec(db, "CREATE TABLE T (A)", nullptr, nullptr, nullptr);
    sqlite3_close(db);
  }
  Check(!DeviceFile::Open(other, DeviceFile::kReadOnly, error), "file: another SQLite file isn't opened: " + error);
  Check(!DeviceFile::Open(folder.path / "none.newtondevice", DeviceFile::kReadOnly, error), "file: no file isn't opened");
}

/*-- delete ------------------------------------------------------------------*/

void TestDelete()
{
  std::string error;
  std::unique_ptr<DeviceFile> file = DeviceFile::CreateInMemory("Ada", error);
  Check(file != nullptr, "delete: in memory " + error);
  if (!file)
    return;
  DeviceRecord device = SetDevice(*file, "Ada", TestInfo(1, 2));
  StoreRecord internal = AddStore(*file, device.pk, RefVar(StoreFrameOf("Internal", 1)));
  StoreRecord card = AddStore(*file, device.pk, RefVar(StoreFrameOf("Card", 2)));
  Check(AddStore(*file, device.pk, RefVar(StoreFrameOf("Card", 2))).pk == card.pk, "delete: a store found by signature");
  std::vector<StoreRecord> sorted = SortedStores(*file);
  Check(sorted.size() == 2 && sorted[0].name == "Internal" && sorted[1].name == "Card", "delete: Internal first");
  SoupRecord soups[3];
  int n = 0;
  for (StoreRecord * store : { &internal, &card, &card }) {
    soups[n] = MakeSoup(store->pk, "Soup" + std::to_string(n), n, RefVar(MakeArray(0)), RefVar(AllocateFrame()));
    file->insert(soups[n]);
    for (int id = 1; id <= 4; ++id) {
      RefVar entry(Person(id, "A", "B"));
      AddEntry(*file, soups[n], entry, FlattenToBytes(entry));
    }
    ++n;
  }
  RefVar imported(Person(0, "New", "One"));
  RemoveSlot(imported, SYMA(_uniqueId));
  AddImportedEntry(*file, soups[0], imported);
  Check(file->entryCount(soups[0].pk) == 5 && soups[0].descr == "No soup definition." && soups[0].appName == "--",
        "delete: a soup without a soupDef");

  Check(file->cropSoup(soups[0].pk, { 2, 4 }) == 2, "delete: crop removed two");
  std::vector<EntryRecord> left = file->entries(soups[0].pk);
  Check(left.size() == 3 && left[0].uniqueId == 2 && left[1].uniqueId == 4 && left[2].uniqueId == kImportIdBase + 1,
        "delete: crop kept those asked for and the imported one");
  file->deleteEntry(left[0].pk);
  Check(file->entryCount(soups[0].pk) == 2, "delete: an entry");
  file->deleteSoup(soups[1].pk);
  Check(!file->soup(soups[1].pk) && file->entryCount(soups[1].pk) == 0 && file->rowCount("ZENTRY") == 6,
        "delete: a soup with its entries");
  file->deleteStore(card.pk);
  Check(file->stores().size() == 1 && file->soups().size() == 1 && file->rowCount("ZENTRY") == 2,
        "delete: a store with its soups and entries");
}

/*-- entries -----------------------------------------------------------------*/

void TestEntries()
{
  std::string error;
  std::unique_ptr<DeviceFile> file = DeviceFile::CreateInMemory("Ada", error);
  if (!file) {
    Check(false, "entries: in memory " + error);
    return;
  }
  DeviceRecord device = SetDevice(*file, "Ada", TestInfo(1, 2));
  StoreRecord store = AddStore(*file, device.pk, RefVar(StoreFrameOf("Internal", 1)));
  SoupRecord soup = MakeSoup(store.pk, "Names", 7, RefVar(StringArray({ "index" })), RefVar(SoupInfo("Names", "Cards", "Names")));
  file->insert(soup);
  RefVar indexes(SoupIndexArray(soup));
  Check(IsArray(indexes) && Length(indexes) == 1, "entries: the soup's indexes");
  RefVar info(SoupInfoFrame(soup));
  Check(IsFrame(info) && NOTNIL(GetFrameSlot(info, Sym("soupDef"))), "entries: the soup's info");

  RefVar entry(Person(10, "Ada", "Lovelace"));
  EntryRecord record = AddEntry(*file, soup, entry, FlattenToBytes(entry));
  SetFrameSlot(entry, Sym("city"), MakeStringFromUTF8("Paris"));
  SetFrameSlot(entry, SYMA(name), NameFrame("Ada", "King"));
  EntryRecord again = AddEntry(*file, soup, entry, FlattenToBytes(entry));
  Check(again.pk == record.pk && again.title == "Ada King" && file->entryCount(soup.pk) == 1,
        "entries: the same _uniqueId updates the entry");
  Check(soup.lastBackupId == 10, "entries: lastBackupId");

  RefVar made(AllocateFrame());
  SetFrameSlot(made, SYMA(class), Sym("person"));
  SetFrameSlot(made, SYMA(name), NameFrame("Charles", "Babbage"));
  EntryRecord first = AddImportedEntry(*file, soup, made);
  RefVar made2(Clone(made));
  RemoveSlot(made2, SYMA(_uniqueId));
  EntryRecord second = AddImportedEntry(*file, soup, made2);
  Check(first.uniqueId == kImportIdBase + 1 && second.uniqueId == kImportIdBase + 2 && soup.lastImportId == kImportIdBase + 2,
        "entries: imported ones get ids from kImportIdBase");
  Check(soup.lastBackupId == 10, "entries: imported ones don't move lastBackupId");
  Check(TimeFromCoreData(first.modTime) / 60 >= Now() / 60 - 1, "entries: imported ones have a modTime of now");
  // NSOF as the Cocoa app wrote it: the id's 30 bits, an immediate 0xFF1B9E04
  RefVar idFrame(AllocateFrame());
  SetFrameSlot(idFrame, SYMA(_uniqueId), MakeUniqueId(kImportIdBase + 1));
  Bytes nsof = FlattenToBytes(idFrame);
  const uint8_t immediate[] = { 0xFF, 0x1B, 0x9E, 0x04 };
  Check(std::search(nsof.begin(), nsof.end(), immediate, immediate + 4) != nsof.end()
        && UniqueIdOf(RefVar(GetFrameSlot(RefVar(UnflattenBytes(nsof)), SYMA(_uniqueId)))) == kImportIdBase + 1,
        "entries: an imported id in NSOF");

  // the Newton took it: its own id
  RefVar added(Clone(made));
  SetFrameSlot(added, SYMA(_uniqueId), MAKEINT(11));
  SetFrameSlot(added, SYMA(_modTime), MAKEINT(52000000));
  UpdateEntry(*file, soup, first, added);
  std::optional<SoupRecord> stored = file->soup(soup.pk);
  Check(first.uniqueId == 11 && file->findEntry(soup.pk, 11) && !file->findEntry(soup.pk, kImportIdBase + 1)
        && stored && stored->lastBackupId == 11 && stored->lastImportId == kImportIdBase + 2,
        "entries: updated with the Newton's _uniqueId");
}

/*-- titles ------------------------------------------------------------------*/

void TestTitles()
{
  RefVar person(Person(1, "Ada", "Lovelace"));
  Check(EntryTitle(person, "Names") == "Ada Lovelace", "titles: a name card");
  RefVar company(AllocateFrame());
  SetFrameSlot(company, SYMA(class), Sym("company"));
  SetFrameSlot(company, Sym("company"), MakeStringFromUTF8("Analytical Engines"));
  Check(EntryTitle(company, "Names") == "Analytical Engines", "titles: a company");
  RefVar worksite(AllocateFrame());
  SetFrameSlot(worksite, SYMA(class), Sym("worksite"));
  SetFrameSlot(worksite, Sym("place"), MakeStringFromUTF8("The Lab"));
  Check(EntryTitle(worksite, "Names") == "The Lab", "titles: a worksite");

  RefVar note(AllocateFrame());
  SetFrameSlot(note, SYMA(title), MakeStringFromUTF8("Shopping"));
  SetFrameSlot(note, SYMA(name), MakeStringFromUTF8("not this"));
  Check(EntryTitle(note, "Notes") == "Shopping", "titles: another soup's title");
  RefVar named(AllocateFrame());
  SetFrameSlot(named, SYMA(name), MakeStringFromUTF8("This one"));
  Check(EntryTitle(named, "Unknown") == "This one", "titles: else the name");
  Check(EntryTitle(RefVar(AllocateFrame()), "Unknown").empty(), "titles: else none");

  RefVar meeting(AllocateFrame());
  SetFrameSlot(meeting, Sym("mtgText"), MakeStringFromUTF8("Tea"));
  SetFrameSlot(meeting, Sym("mtgStartDate"), MAKEINT(50000000));
  Check(EntryTitle(meeting, "Calendar") == "Tea", "titles: a meeting");
  const std::vector<SoupColumn> & columns = SoupColumns("Calendar");
  Check(columns.size() == 2 && columns[1].type == "dateRef"
        && SlotText(RefVar(GetFrameSlot(meeting, Sym("mtgStartDate"))), columns[1].type) == FormatDateTime(TimeFromNewton(50000000)),
        "titles: a meeting's time");
  Check(SoupColumns("Whatever").size() == 1 && SoupColumns("Whatever")[0].slot == "tag", "titles: the default columns");
  Check(SoupShowsLabels("Notes") && !SoupShowsLabels("Calendar"), "titles: which show labels");

  Check(SlotText(RefVar(MAKEINT(3600)), "GMT") == "+01:00" && SlotText(RefVar(MAKEINT(-19800)), "GMT") == "-05:30",
        "titles: GMT offsets");
  Check(SlotText(RefVar(Sym("calendar")), "symbol") == "calendar", "titles: a symbol");
  RefVar to(MakeArray(0));
  RefVar recipient(AllocateFrame());
  SetFrameSlot(recipient, SYMA(name), NameFrame("Mary", "Somerville"));
  AddArraySlot(to, recipient);
  Check(SlotText(to, "nameRefArray") == "Mary Somerville", "titles: a recipient");
  Check(SlotText(RefVar(MAKEINT(5)), "string").empty() && SlotText(RefVar(NILREF), "dateRef").empty(), "titles: wrong types");
  RefVar umlaut(AllocateFrame());
  SetFrameSlot(umlaut, SYMA(title), MakeStringFromUTF8("Gr\xC3\xBC\xC3\x9F" "e"));
  Check(EntryTitle(umlaut, "InBox") == "Gr\xC3\xBC\xC3\x9F" "e", "titles: non-ASCII");
}

/*-- backup ------------------------------------------------------------------*/

void AppendWord(Bytes & ioData, uint32_t inWord, bool inBigEndian)
{
  for (int i = 0; i < 4; ++i)
    ioData.push_back((uint8_t)(inBigEndian ? inWord >> (24 - 8 * i) : inWord >> (8 * i)));
}

void AppendRef(Bytes & ioData, RefArg inObj)
{
  Bytes nsof = FlattenToBytes(inObj);
  ioData.insert(ioData.end(), nsof.begin(), nsof.end());
}

/** A backup as Newton Connection 1 wrote them. */
Bytes MakeBackup(bool inBigEndian)
{
  Bytes data = { 'b', 'a', 'c', 'k', 'u', 'p', '0', 0 };
  AppendWord(data, 2, inBigEndian);
  AppendWord(data, 0x01000000, inBigEndian);
  AppendWord(data, 0x10003000, inBigEndian);

  RefVar store(StoreFrameOf("Internal", 4242));
  SetFrameSlot(store, Sym("soups"), StringArray({ "Names", "Packages", "Lost" }));
  RefVar names(AllocateFrame());
  SetFrameSlot(names, SYMA(name), MakeStringFromUTF8("Names"));
  SetFrameSlot(names, Sym("soups"), StringArray({ "Names" }));
  RefVar packages(AllocateFrame());
  SetFrameSlot(packages, SYMA(name), MakeStringFromUTF8("Packages"));
  SetFrameSlot(packages, Sym("soups"), StringArray({ "Packages" }));
  RefVar apps(MakeArray(0));
  AddArraySlot(apps, names);
  AddArraySlot(apps, packages);
  SetFrameSlot(store, Sym("apps"), apps);
  AppendRef(data, store);

  // Names
  AppendRef(data, RefVar(StringArray({ "index" })));
  AppendRef(data, RefVar(SoupInfo("Names", "Name cards", "Names")));
  AppendRef(data, RefVar(Person(1, "Ada", "Lovelace")));
  AppendRef(data, RefVar(Person(2, "Charles", "Babbage")));
  RefVar company(AllocateFrame());
  SetFrameSlot(company, SYMA(class), Sym("company"));
  SetFrameSlot(company, Sym("company"), MakeStringFromUTF8("Engines Ltd"));
  SetFrameSlot(company, SYMA(_uniqueId), MAKEINT(3));
  AppendRef(data, company);
  AppendRef(data, RefVar(NILREF));

  // Packages: only those with a pkgRef
  AppendRef(data, RefVar(MakeArray(0)));
  AppendRef(data, RefVar(AllocateFrame()));
  RefVar package(AllocateFrame());
  SetFrameSlot(package, Sym("packageName"), MakeStringFromUTF8("Tetris"));
  RefVar pkgRef(AllocateBinary(Sym("package"), 16));
  std::memcpy(BinaryData(pkgRef), "package0 a test", 16);
  SetFrameSlot(package, Sym("pkgRef"), pkgRef);
  SetFrameSlot(package, SYMA(_uniqueId), MAKEINT(1));
  AppendRef(data, package);
  RefVar other(AllocateFrame());
  SetFrameSlot(other, Sym("packageName"), MakeStringFromUTF8("not a package"));
  SetFrameSlot(other, SYMA(_uniqueId), MAKEINT(2));
  AppendRef(data, other);
  AppendRef(data, RefVar(NILREF));
  return data;
}

void TestBackup()
{
  for (bool bigEndian : { false, true }) {
    std::string how = bigEndian ? " (PowerPC)" : " (Intel)";
    std::string error;
    std::unique_ptr<DeviceFile> file = ImportBackup(MakeBackup(bigEndian), "My Newton", error);
    Check(file != nullptr, "backup: read" + how + " " + error);
    if (!file)
      continue;
    std::optional<DeviceRecord> device = file->device();
    Check(device && device->name == "My Newton" && NewtonInfoOf(*device).fMachineType == 0x10003000
          && NewtonInfoOf(*device).fManufacturer == 0x01000000 && NewtonInfoOf(*device).fNOSVersion == 2,
          "backup: the device" + how);
    std::vector<StoreRecord> stores = file->stores();
    Check(stores.size() == 1 && stores[0].signature == 4242 && stores[0].password == std::string(), "backup: the store" + how);
    Check(file->apps().size() == 2, "backup: the apps" + how);
    std::optional<SoupRecord> names = file->findSoup(stores.at(0).pk, "Names");
    Check(names && names->descr == "Name cards" && names->lastBackupId == 3 && file->entryCount(names->pk) == 3,
          "backup: the Names soup" + how);
    if (names) {
      std::vector<EntryRecord> entries = file->entries(names->pk);
      Check(entries.size() == 3 && entries[0].title == "Ada Lovelace" && entries[2].title == "Engines Ltd",
            "backup: the Names entries' titles" + how);
    }
    std::optional<SoupRecord> packages = file->findSoup(stores.at(0).pk, "Packages");
    std::vector<EntryRecord> packageEntries = packages ? file->entries(packages->pk, true) : std::vector<EntryRecord>();
    Check(packageEntries.size() == 1 && packageEntries[0].refClass == "*package*" && packageEntries[0].title == "Tetris",
          "backup: only the packages" + how);
    if (!packageEntries.empty()) {
      RefVar frame(EntryFrame(packageEntries[0]));
      Check(UTF8FromSymbol(GetFrameSlot(frame, SYMA(class))) == "*package*", "backup: a package's class" + how);
    }
    std::optional<SoupRecord> lost = file->findSoup(stores.at(0).pk, "Lost");
    Check(lost && lost->descr == "Orphan soup - no definition." && file->entryCount(lost->pk) == 0, "backup: an orphan soup" + how);
  }

  std::string error;
  Bytes data = MakeBackup(false);
  data.resize(data.size() - 30);
  Check(!ImportBackup(data, "Cut", error) && !error.empty(), "backup: one cut short: " + error);
  Check(!ImportBackup(Bytes{ 'n', 'o', 'p', 'e' }, "Nope", error), "backup: not a backup");

  TempFolder folder("backup");
  fs::path path = folder.path / "Old Newton.nbku";
  {
    Bytes backup = MakeBackup(false);
    std::ofstream out(path, std::ios::binary);
    out.write((const char *)backup.data(), (std::streamsize)backup.size());
  }
  std::unique_ptr<DeviceFile> file = ImportBackup(path, error);
  Check(file && file->device() && file->device()->name == "Old Newton", "backup: from a file " + error);
}

/*-- devices -----------------------------------------------------------------*/

void TestDevices()
{
  TempFolder folder("devices");
  fs::path devices = folder.path / "Devices";
  std::string error;
  NewtonInfo info = TestInfo(0x00004E65, 0x7774);
  fs::path path;
  {
    std::unique_ptr<DeviceFile> file = OpenDeviceFileFor(devices, info, "Ada", error);
    Check(file != nullptr, "devices: a new Newton's file " + error);
    if (!file)
      return;
    path = file->path();
    Check(path.filename() == "0000-4E65-0000-7774.newtondevice" && file->device() && file->device()->name == "Ada",
          "devices: named after the Newton");
  }
  {
    std::unique_ptr<DeviceFile> file = OpenDeviceFileFor(devices, info, "Ada L.", error);
    Check(file && file->path() == path && file->newtonName() == "Ada L." && file->rowCount("ZDEVICE") == 1,
          "devices: the same Newton again, renamed");
  }
  {
    std::unique_ptr<DeviceFile> file = OpenDeviceFileFor(devices, TestInfo(0, 0), "Bob", error);
    Check(file && file->path().filename() == "123456.newtondevice", "devices: a Newton without a serial number");
  }
  {
    std::ofstream out(devices / "broken.newtondevice", std::ios::binary);
    out << "this is not a device file";
  }
  std::vector<DeviceFileInfo> list = ListDeviceFiles(devices);
  Check(list.size() == 2 && list[0].newtonId == "0000-4E65-0000-7774" && list[0].newtonName == "Ada L."
        && list[1].newtonName == "Bob", "devices: the list leaves out what isn't a device file");
  Check(FindDeviceFile(devices, "123456") == devices / "123456.newtondevice" && FindDeviceFile(devices, "nope").empty(),
        "devices: found by NewtonId");
  Check(ListDeviceFiles(folder.path / "none").empty(), "devices: no folder, no devices");
}

/*-- large binaries ----------------------------------------------------------*/

void TestLarge()
{
  // an entry with a large binary (a VBO), as a Newton sends one: NSOF
  // kNSLargeBinary (12), its class, compressed?, the stream's size, the
  // compander's name and parameters' sizes, 0, the name, then the stream (a
  // compander, a Newton sends its name with a T for the C)
  const std::string compander = "TSimpleStoreCompander";
  Bytes nsof = { 2, 6, 1, 7, 4, 'd', 'a', 't', 'a', 12, 7, 4, 'b', 'l', 'o', 'b', 0 };
  const uint32_t words[] = { 64, (uint32_t)compander.size(), 0, 0 };
  for (uint32_t w : words)
    AppendWord(nsof, w, true);
  nsof.insert(nsof.end(), compander.begin(), compander.end());
  for (int i = 0; i < 64; ++i)
    nsof.push_back((uint8_t)(i * 3));
  RefVar entry(UnflattenBytes(nsof));
  RefVar data(IsFrame(entry) ? GetFrameSlot(entry, Sym("data")) : (Ref)NILREF);
  bool same = IsBinary(data) && Length(data) == 64;
  if (same) {
    const uint8_t * bytes = (const uint8_t *)BinaryData(data);
    for (int i = 0; i < 64; ++i)
      same = same && bytes[i] == (uint8_t)(i * 3);
  }
  Check(same && UTF8FromSymbol(ClassOf(data)) == "blob", "large: an entry's large binary read");
  Bytes back = FlattenToBytes(entry);
  RefVar again(UnflattenBytes(back));
  Check(IsFrame(again) && IsBinary(GetFrameSlot(again, Sym("data"))) && Length(GetFrameSlot(again, Sym("data"))) == 64,
        "large: and written again");
}

/*-- restore -----------------------------------------------------------------*/

void TestRestore()
{
  std::string error;
  std::unique_ptr<DeviceFile> file = ImportBackup(MakeBackup(false), "Ada", error);
  if (!file) {
    Check(false, "restore: a backup " + error);
    return;
  }
  // a card too, with an app of its own
  int64_t devicePk = file->device() ? file->device()->pk : 0;
  StoreRecord card = AddStore(*file, devicePk, RefVar(StoreFrameOf("Card", 77)));
  SoupRecord soup = MakeSoup(card.pk, "Notes", 1, RefVar(MakeArray(0)), RefVar(SoupInfo("Notes", "Notes", "Notepad")));
  soup.appPk = FindOrAddApp(*file, "Notepad").pk;
  file->insert(soup);

  StoreRecord internal = file->stores().at(0);
  std::vector<AppRecord> apps = file->storeApps(internal.pk, true);
  Check(apps.size() == 2 && apps[0].name == "Names" && apps[1].name == "Packages", "restore: a store's apps");
  std::vector<EntryRecord> packages = file->storePackages(internal.pk);
  Check(packages.size() == 1 && packages[0].title == "Tetris" && packages[0].refData.empty(), "restore: a store's packages");

  // the Newton has a card and its internal store
  std::vector<RestoreChoice> targets = RestoreChoices(*file, { card, internal });
  Check(targets.size() == 2 && targets[0].store.name == "Internal" && targets[1].store.name == "Card",
        "restore: the Newton's stores, Internal first");
  if (targets.size() != 2)
    return;
  const std::vector<RestoreFrom> & toInternal = targets[0].sources;
  Check(toInternal.size() == 1 && toInternal[0].store.pk == internal.pk && toInternal[0].apps.size() == 1
        && toInternal[0].apps[0].name == "Names" && toInternal[0].packages.size() == 1,
        "restore: to the internal store, the internal one's apps (but Packages) and packages");
  const std::vector<RestoreFrom> & toCard = targets[1].sources;
  Check(toCard.size() == 1 && toCard[0].store.pk == card.pk && toCard[0].apps.size() == 1 && toCard[0].apps[0].name == "Notepad"
        && toCard[0].packages.empty(), "restore: to a card, the cards'");
}

/*-- fixture -----------------------------------------------------------------*/

std::string gFixture;      // tests/fixtures/einstein.newtondevice

/** A backup the Cocoa NCX made from Einstein (MessagePad 2000, NewtonOS
    2.1, an internal store and a flash card), read and changed by NCX. */
void TestFixture()
{
  TempFolder folder("fixture");
  fs::path path = folder.path / "0000-4E65-7774-6F6E.newtondevice";
  fs::copy_file(PathFromUTF8(gFixture), path);
  std::string error;
  {
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadOnly, error);
    Check(file != nullptr, "fixture: open " + error);
    if (!file)
      return;
    Check(file->newtonId() == "0000-4E65-7774-6F6E", "fixture: NewtonId " + file->newtonId());
    std::optional<DeviceRecord> device = file->device();
    Check(device && MachineTypeName(NewtonInfoOf(*device)) == "MessagePad 2000"
          && DeviceId(NewtonInfoOf(*device)) == "0000-4E65-7774-6F6E", "fixture: the device");
    std::vector<StoreRecord> stores = SortedStores(*file);
    Check(stores.size() == 2 && stores[0].name == "Internal" && IsInternal(stores[0]) && !IsInternal(stores[1])
          && stores[1].kind == "Flash storage card", "fixture: an internal store and a card");
    // the Cocoa NCX deleted the card's soups but not their entries (the
    // model's delete rules only cut the links): 22 entries in no soup
    Check(file->rowCount("ZSOUP") == 21 && file->rowCount("ZENTRY") == 91 && file->rowCount("ZAPP") == 12,
          "fixture: 21 soups, 91 entries (22 in no soup), 12 apps");

    size_t entries = 0, unread = 0, titles = 0, classes = 0, ids = 0, packages = 0, soupData = 0, backupIds = 0;
    for (const SoupRecord & soup : file->soups()) {
      RefVar info(UnflattenBytes(soup.info)), indexes(UnflattenBytes(soup.indexes));
      if (IsFrame(info) && IsArray(indexes))
        ++soupData;
      int64_t highest = 0;
      for (const EntryRecord & entry : file->entries(soup.pk, true)) {
        ++entries;
        RefVar frame(EntryFrame(entry));
        if (!IsFrame(frame)) {
          ++unread;
          continue;
        }
        if (entry.uniqueId < kImportIdBase && entry.uniqueId > highest)
          highest = entry.uniqueId;
        std::string title = EntryTitle(frame, soup.name);
        if (title == entry.title.value_or(""))
          ++titles;
        else
          std::printf("        %s: title \"%s\", the Cocoa NCX's \"%s\"\n", soup.name.c_str(), title.c_str(),
                      entry.title.value_or("").c_str());
        // the class symbol, else *package* for a package, else none
        RefVar entryClass(GetFrameSlot(frame, SYMA(class)));
        std::string cls = IsSymbol(entryClass) ? UTF8FromSymbol(entryClass)
                        : FrameHasSlot(frame, MakeSymbol("pkgRef")) ? std::string("*package*") : std::string();
        if (cls == entry.refClass.value_or(""))
          ++classes;
        if (UniqueIdOf(RefVar(GetFrameSlot(frame, SYMA(_uniqueId)))) == entry.uniqueId)
          ++ids;
        if (entry.refClass == std::string("*package*")) {
          RefVar package(GetFrameSlot(frame, MakeSymbol("pkgRef")));
          if (IsBinary(package) && Length(package) > 8 && std::memcmp(BinaryData(package), "package", 7) == 0)
            ++packages;
        }
      }
      if (soup.lastBackupId >= highest)
        ++backupIds;
    }
    Check(entries == 69 && unread == 0, "fixture: every entry reads");
    Check(titles == entries, "fixture: titles as the Cocoa NCX made them (" + std::to_string(titles) + " of "
          + std::to_string(entries) + ")");
    Check(classes == entries, "fixture: classes as the Cocoa NCX had them (" + std::to_string(classes) + ")");
    Check(ids == entries, "fixture: _uniqueIds (" + std::to_string(ids) + ")");
    Check(packages == 18, "fixture: 18 packages, decompressed (" + std::to_string(packages) + ")");
    Check(soupData == 21, "fixture: every soup's info and indexes (" + std::to_string(soupData) + ")");
    Check(backupIds == 21, "fixture: every soup's lastBackupId (" + std::to_string(backupIds) + ")");

    std::vector<RestoreChoice> choices = RestoreChoices(*file, stores);
    Check(choices.size() == 2 && choices[0].sources.size() == 1 && choices[0].sources[0].packages.size() == 18
          && !choices[0].sources[0].apps.empty() && choices[1].sources.size() == 1, "fixture: restore choices");
  }

  // NCX changes it
  {
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadWrite, error);
    Check(file != nullptr, "fixture: open for writing " + error);
    if (!file)
      return;
    DeviceFile::Transaction transaction(*file);
    StoreRecord internal = SortedStores(*file).at(0);
    std::optional<SoupRecord> notes = file->findSoup(internal.pk, "Notes");
    std::optional<SoupRecord> cache = file->findSoup(internal.pk, "Courier Cache:40Hz");
    if (!notes || !cache) {
      Check(false, "fixture: the Notes and Courier Cache soups");
      return;
    }
    RefVar note(AllocateFrame());
    SetFrameSlot(note, SYMA(class), Sym("paragraph"));
    SetFrameSlot(note, SYMA(title), MakeStringFromUTF8("Added by NCX"));
    EntryRecord added = AddImportedEntry(*file, *notes, note);
    file->deleteSoup(cache->pk);
    internal.usedSize += 1000;
    file->update(internal);
    file->setIdentity(file->newtonId(), "Einstein (changed)");
    transaction.commit();
    Check(added.uniqueId == kImportIdBase + 1 && added.title == "Added by NCX", "fixture: an entry added");
  }
  Check(PrimaryKeysAgree(path), "fixture: Z_PRIMARYKEY.Z_MAX follows the rows");
  {
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadOnly, error);
    Check(file && coredata::IsThisModel(file->metadata()) && file->newtonId() == "0000-4E65-7774-6F6E"
          && file->newtonName() == "Einstein (changed)", "fixture: reopened, the metadata kept");
    size_t inSoups = 0;
    if (file)
      for (const SoupRecord & soup : file->soups())
        inSoups += file->entryCount(soup.pk);
    Check(file && file->rowCount("ZSOUP") == 20 && inSoups == 69 - 9 + 1 && file->rowCount("ZENTRY") == 91 - 9 + 1,
          "fixture: reopened, the changes there");
  }
}

} // namespace


int main(int argc, char * argv[])
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::string section = argc > 1 ? argv[1] : "all";
  gFixture = argc > 2 ? argv[2] : "";
  InitCore();
  if (section == "write-backup" && argc > 2) {
    // a backup to try things on (ncx-cli convert, the Core Data check)
    Bytes backup = MakeBackup(false);
    std::ofstream out(PathFromUTF8(argv[2]), std::ios::binary);
    out.write((const char *)backup.data(), (std::streamsize)backup.size());
    return out ? 0 : 1;
  }
  struct { const char * name; void (*test)(); } sections[] = {
    { "plist", TestPlist }, { "file", TestFile }, { "delete", TestDelete }, { "entries", TestEntries },
    { "titles", TestTitles }, { "backup", TestBackup }, { "devices", TestDevices }, { "large", TestLarge },
    { "restore", TestRestore }, { "fixture", TestFixture } };
  bool found = false;
  for (auto & s : sections)
    if ((section == "all" && (std::strcmp(s.name, "fixture") != 0 || !gFixture.empty())) || section == s.name) {
      s.test();
      found = true;
    }
  if (!found) {
    std::fprintf(stderr, "no section \"%s\"\n", section.c_str());
    return 2;
  }
  std::printf("%s\n", gFailed ? "FAILED" : "all ok");
  return gFailed ? 1 : 0;
}
