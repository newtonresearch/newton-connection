/*
 File: DeviceFile.cc

 .newtondevice files on SQLite, in Core Data's schema (DeviceFile.h).
 */

#include "model/DeviceFile.h"
#include "model/CoreDataModel.h"
#include "base/Log.h"
#include "platform/AppFolders.h"

#include <sqlite3.h>

#include <set>
#include <system_error>

namespace ncx {

namespace {

/*-- a prepared statement ----------------------------------------------------*/

class Stmt
{
public:
  Stmt(sqlite3 * inDb, const char * inSQL) : mDb(inDb)
  {
    if (sqlite3_prepare_v2(inDb, inSQL, -1, &mStmt, nullptr) != SQLITE_OK)
      throw ModelError(std::string("SQL: ") + sqlite3_errmsg(inDb) + " (" + inSQL + ")");
  }
  ~Stmt() { sqlite3_finalize(mStmt); }
  Stmt(const Stmt &) = delete;
  Stmt & operator=(const Stmt &) = delete;

  Stmt & bind(int inIndex, int64_t inValue) { check(sqlite3_bind_int64(mStmt, inIndex, inValue)); return *this; }
  Stmt & bind(int inIndex, double inValue) { check(sqlite3_bind_double(mStmt, inIndex, inValue)); return *this; }
  Stmt & bind(int inIndex, const std::string & inValue)
  {
    check(sqlite3_bind_text(mStmt, inIndex, inValue.data(), (int)inValue.size(), SQLITE_TRANSIENT));
    return *this;
  }
  Stmt & bind(int inIndex, const Bytes & inValue)
  {
    // an empty blob, not NULL (Core Data writes zero-length data so)
    check(sqlite3_bind_blob(mStmt, inIndex, inValue.empty() ? "" : (const void *)inValue.data(), (int)inValue.size(), SQLITE_TRANSIENT));
    return *this;
  }
  Stmt & bindNull(int inIndex) { check(sqlite3_bind_null(mStmt, inIndex)); return *this; }
  Stmt & bindBool(int inIndex, bool inValue) { return bind(inIndex, (int64_t)(inValue ? 1 : 0)); }
  /** A reference: NULL for 0. */
  Stmt & bindRef(int inIndex, int64_t inPk) { return inPk ? bind(inIndex, inPk) : bindNull(inIndex); }
  template <typename T> Stmt & bind(int inIndex, const std::optional<T> & inValue)
  {
    return inValue ? bind(inIndex, *inValue) : bindNull(inIndex);
  }

  /** True for a row, false at the end. */
  bool step()
  {
    int result = sqlite3_step(mStmt);
    if (result == SQLITE_ROW)
      return true;
    if (result == SQLITE_DONE)
      return false;
    throw ModelError(std::string("SQL: ") + sqlite3_errmsg(mDb));
  }
  void run() { step(); }

  bool isNull(int inColumn) { return sqlite3_column_type(mStmt, inColumn) == SQLITE_NULL; }
  int64_t i64(int inColumn) { return sqlite3_column_int64(mStmt, inColumn); }
  double real(int inColumn) { return sqlite3_column_double(mStmt, inColumn); }
  std::string text(int inColumn)
  {
    const unsigned char * t = sqlite3_column_text(mStmt, inColumn);
    return t ? std::string((const char *)t, (size_t)sqlite3_column_bytes(mStmt, inColumn)) : std::string();
  }
  Bytes blob(int inColumn)
  {
    const uint8_t * b = static_cast<const uint8_t *>(sqlite3_column_blob(mStmt, inColumn));
    int n = sqlite3_column_bytes(mStmt, inColumn);
    return b ? Bytes(b, b + n) : Bytes();
  }
  std::optional<int64_t> optI64(int c) { return isNull(c) ? std::nullopt : std::optional<int64_t>(i64(c)); }
  std::optional<double> optReal(int c) { return isNull(c) ? std::nullopt : std::optional<double>(real(c)); }
  std::optional<std::string> optText(int c) { return isNull(c) ? std::nullopt : std::optional<std::string>(text(c)); }
  std::optional<Bytes> optBlob(int c) { return isNull(c) ? std::nullopt : std::optional<Bytes>(blob(c)); }

private:
  void check(int inResult)
  {
    if (inResult != SQLITE_OK)
      throw ModelError(std::string("SQL: ") + sqlite3_errmsg(mDb));
  }
  sqlite3 * mDb;
  sqlite3_stmt * mStmt = nullptr;
};


/** A savepoint: one write that takes several statements (a pk and its
    row) happens whole or not at all, in a transaction or not. */
class Savepoint
{
public:
  explicit Savepoint(sqlite3 * inDb) : mDb(inDb) { sqlite3_exec(mDb, "SAVEPOINT ncx_write", nullptr, nullptr, nullptr); }
  ~Savepoint()
  {
    if (!mDone) {
      sqlite3_exec(mDb, "ROLLBACK TO ncx_write", nullptr, nullptr, nullptr);
      sqlite3_exec(mDb, "RELEASE ncx_write", nullptr, nullptr, nullptr);
    }
  }
  void release()
  {
    sqlite3_exec(mDb, "RELEASE ncx_write", nullptr, nullptr, nullptr);
    mDone = true;
  }
private:
  sqlite3 * mDb;
  bool mDone = false;
};


/*-- reading rows ------------------------------------------------------------*/

const char * const kDeviceColumns =
  "Z_PK, ZNAME, ZINFO, ZOSINFO, ZPROCESSOR, ZMANUFACTUREDATE, ZBACKUPDATE, ZBACKUPTIME, ZBACKUPERROR, ZSYNCMODE, ZUSER";
DeviceRecord ReadDevice(Stmt & s)
{
  DeviceRecord d;
  d.pk = s.i64(0); d.name = s.text(1); d.info = s.blob(2); d.osInfo = s.optText(3); d.processor = s.optText(4);
  d.manufactureDate = s.optReal(5); d.backupDate = s.optReal(6); d.backupTime = s.optI64(7); d.backupError = s.optI64(8);
  d.syncMode = s.i64(9); d.userPk = s.i64(10);
  return d;
}

const char * const kStoreColumns =
  "Z_PK, ZDEVICE, ZNAME, ZKIND, ZSTOREPASSWORD, ZSIGNATURE, ZSTOREVERSION, ZTOTALSIZE, ZUSEDSIZE, ZREADONLY, ZDEFAULTSTORE";
StoreRecord ReadStore(Stmt & s)
{
  StoreRecord r;
  r.pk = s.i64(0); r.devicePk = s.i64(1); r.name = s.text(2); r.kind = s.text(3); r.password = s.optText(4);
  r.signature = s.i64(5); r.storeVersion = s.i64(6); r.totalSize = s.i64(7); r.usedSize = s.i64(8);
  r.readOnly = s.i64(9) != 0; r.defaultStore = s.i64(10) != 0;
  return r;
}

const char * const kSoupColumns =
  "Z_PK, ZSTORE, ZAPP, ZNAME, ZDESCR, ZAPPNAME, ZINDEXES, ZINFO, ZSIGNATURE, ZLASTBACKUPID, ZLASTBACKUPTIME, "
  "ZLASTIMPORTID, ZPREVSYNCHTIME, ZPREVSYNCHIDDATA";
SoupRecord ReadSoup(Stmt & s)
{
  SoupRecord r;
  r.pk = s.i64(0); r.storePk = s.i64(1); r.appPk = s.i64(2); r.name = s.text(3); r.descr = s.text(4); r.appName = s.text(5);
  r.indexes = s.blob(6); r.info = s.blob(7); r.signature = s.i64(8); r.lastBackupId = s.i64(9); r.lastBackupTime = s.i64(10);
  r.lastImportId = s.optI64(11); r.prevSynchTime = s.optReal(12); r.prevSynchIdData = s.optBlob(13);
  return r;
}

EntryRecord ReadEntry(Stmt & s, bool inWithData)
{
  EntryRecord r;
  r.pk = s.i64(0); r.soupPk = s.i64(1); r.uniqueId = s.i64(2); r.modTime = s.real(3);
  r.refClass = s.optText(4); r.title = s.optText(5);
  if (inWithData)
    r.refData = s.blob(6);
  return r;
}

std::string EntryColumns(bool inWithData)
{
  return std::string("Z_PK, ZSOUP, ZUNIQUEID, ZMODTIME, ZREFCLASS, ZTITLE") + (inWithData ? ", ZREFDATA" : "");
}

} // namespace


/*-- opening -----------------------------------------------------------------*/

DeviceFile::~DeviceFile()
{
  if (mDb)
    sqlite3_close(mDb);
}


void DeviceFile::exec(const char * inSQL)
{
  char * message = nullptr;
  if (sqlite3_exec(mDb, inSQL, nullptr, nullptr, &message) != SQLITE_OK) {
    std::string text = message ? message : "?";
    sqlite3_free(message);
    throw ModelError("SQL: " + text + " (" + inSQL + ")");
  }
}


bool DeviceFile::load(std::string & outError)
{
  try {
    Stmt entities(mDb, "SELECT Z_ENT, Z_NAME FROM Z_PRIMARYKEY");
    while (entities.step())
      mEntities[entities.text(1)] = (int)entities.i64(0);
    for (const std::string & name : coredata::EntityNames())
      if (!mEntities.count(name)) {
        outError = "no entity " + name + ": not a Newton Connection file";
        return false;
      }
    Stmt metadata(mDb, "SELECT Z_PLIST FROM Z_METADATA ORDER BY Z_VERSION LIMIT 1");
    if (!metadata.step() || !Plist::Read(metadata.blob(0), mMetadata, outError)) {
      outError = "no metadata: " + outError;
      return false;
    }
    if (!coredata::IsThisModel(mMetadata)) {
      outError = "made with another model (not a Newton Connection file)";
      return false;
    }
  } catch (const ModelError & error) {
    outError = std::string("not a Newton Connection file (") + error.what() + ")";
    return false;
  }
  return true;
}


std::unique_ptr<DeviceFile> DeviceFile::Open(const std::filesystem::path & inPath, Mode inMode, std::string & outError)
{
  std::unique_ptr<DeviceFile> file(new DeviceFile());
  file->mPath = inPath;
  file->mReadOnly = inMode == kReadOnly;
  int flags = inMode == kReadOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;
  if (sqlite3_open_v2(UTF8FromPath(inPath).c_str(), &file->mDb, flags, nullptr) != SQLITE_OK) {
    outError = "can't open " + UTF8FromPath(inPath) + ": " + (file->mDb ? sqlite3_errmsg(file->mDb) : "no memory");
    return nullptr;
  }
  sqlite3_busy_timeout(file->mDb, 2000);
  if (!file->load(outError))
    return nullptr;
  return file;
}


std::unique_ptr<DeviceFile> DeviceFile::Create(const std::filesystem::path & inPath, const std::string & inNewtonId,
                                               const std::string & inNewtonName, std::string & outError)
{
  std::error_code exists;
  if (std::filesystem::exists(inPath, exists)) {
    outError = UTF8FromPath(inPath) + " is there already";
    return nullptr;
  }
  std::unique_ptr<DeviceFile> file(new DeviceFile());
  file->mPath = inPath;
  bool memory = inPath.empty();
  std::string name = memory ? std::string(":memory:") : UTF8FromPath(inPath);
  if (sqlite3_open_v2(name.c_str(), &file->mDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
    outError = "can't make " + name + ": " + (file->mDb ? sqlite3_errmsg(file->mDb) : "no memory");
    return nullptr;
  }
  try {
    // as Core Data makes a store: incremental auto vacuum (before any
    // table), write-ahead log
    file->exec("PRAGMA auto_vacuum = 2");
    if (!memory)
      file->exec("PRAGMA journal_mode = WAL");
    file->exec("BEGIN");
    for (const std::string & statement : coredata::Schema())
      file->exec(statement.c_str());
    int number = 1;
    for (const std::string & entity : coredata::EntityNames()) {
      Stmt row(file->mDb, "INSERT INTO Z_PRIMARYKEY (Z_ENT, Z_NAME, Z_SUPER, Z_MAX) VALUES (?, ?, 0, 0)");
      row.bind(1, (int64_t)number++).bind(2, entity).run();
    }
    file->mMetadata = coredata::MakeMetadata(inNewtonId, inNewtonName);
    Stmt metadata(file->mDb, "INSERT INTO Z_METADATA (Z_VERSION, Z_UUID, Z_PLIST) VALUES (1, ?, ?)");
    metadata.bind(1, coredata::MakeUUID()).bind(2, file->mMetadata.write()).run();
    file->exec("COMMIT");
  } catch (const ModelError & error) {
    outError = error.what();
    file.reset();
    if (!memory) {
      std::error_code ignored;
      std::filesystem::remove(inPath, ignored);
    }
    return nullptr;
  }
  if (!file->load(outError))
    return nullptr;
  return file;
}


std::unique_ptr<DeviceFile> DeviceFile::CreateInMemory(const std::string & inNewtonName, std::string & outError)
{
  return Create(std::filesystem::path(), std::string(), inNewtonName, outError);
}


void DeviceFile::saveAs(const std::filesystem::path & inPath)
{
  std::error_code exists;
  if (std::filesystem::exists(inPath, exists))
    throw ModelError(UTF8FromPath(inPath) + " is there already");
  sqlite3 * copy = nullptr;
  std::string error;
  if (sqlite3_open_v2(UTF8FromPath(inPath).c_str(), &copy, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK) {
    // page by page: auto_vacuum comes with the pages, the journal doesn't
    sqlite3_backup * backup = sqlite3_backup_init(copy, "main", mDb, "main");
    if (!backup || sqlite3_backup_step(backup, -1) != SQLITE_DONE)
      error = sqlite3_errmsg(copy);
    sqlite3_backup_finish(backup);
    if (error.empty() && sqlite3_exec(copy, "PRAGMA journal_mode = WAL", nullptr, nullptr, nullptr) != SQLITE_OK)
      error = sqlite3_errmsg(copy);
  } else {
    error = copy ? sqlite3_errmsg(copy) : "no memory";
  }
  sqlite3_close(copy);
  if (!error.empty()) {
    std::error_code ignored;
    std::filesystem::remove(inPath, ignored);
    throw ModelError("can't save " + UTF8FromPath(inPath) + ": " + error);
  }
}


/*-- metadata ----------------------------------------------------------------*/

std::string DeviceFile::newtonId() const
{
  const Plist * id = mMetadata.find("NewtonId");
  return id && id->type() == Plist::kString ? id->asString() : std::string();
}


std::string DeviceFile::newtonName() const
{
  const Plist * name = mMetadata.find("NewtonName");
  return name && name->type() == Plist::kString ? name->asString() : std::string();
}


void DeviceFile::setIdentity(const std::string & inNewtonId, const std::string & inNewtonName)
{
  mMetadata.set("NewtonId", Plist::String(inNewtonId));
  mMetadata.set("NewtonName", Plist::String(inNewtonName));
  writeMetadata();
}


void DeviceFile::writeMetadata()
{
  if (mReadOnly)
    throw ModelError("the file is open for reading only");
  Stmt metadata(mDb, "UPDATE Z_METADATA SET Z_PLIST = ? WHERE Z_VERSION = (SELECT MIN(Z_VERSION) FROM Z_METADATA)");
  metadata.bind(1, mMetadata.write()).run();
}


/*-- reading -----------------------------------------------------------------*/

std::optional<DeviceRecord> DeviceFile::device()
{
  Stmt s(mDb, (std::string("SELECT ") + kDeviceColumns + " FROM ZDEVICE ORDER BY Z_PK LIMIT 1").c_str());
  if (!s.step())
    return std::nullopt;
  return ReadDevice(s);
}


std::optional<UserInfoRecord> DeviceFile::userInfo(int64_t inPk)
{
  Stmt s(mDb, "SELECT Z_PK, ZDEVICE, ZFOLDERS, ZFONT FROM ZUSERINFO WHERE Z_PK = ?");
  s.bind(1, inPk);
  if (!s.step())
    return std::nullopt;
  UserInfoRecord r;
  r.pk = s.i64(0); r.devicePk = s.i64(1); r.folders = s.optBlob(2); r.font = s.optBlob(3);
  return r;
}


std::vector<StoreRecord> DeviceFile::stores()
{
  std::vector<StoreRecord> stores;
  Stmt s(mDb, (std::string("SELECT ") + kStoreColumns + " FROM ZSTORE ORDER BY Z_PK").c_str());
  while (s.step())
    stores.push_back(ReadStore(s));
  return stores;
}


std::vector<AppRecord> DeviceFile::apps()
{
  std::vector<AppRecord> apps;
  Stmt s(mDb, "SELECT Z_PK, ZNAME FROM ZAPP ORDER BY ZNAME");
  while (s.step())
    apps.push_back(AppRecord{ s.i64(0), s.text(1) });
  return apps;
}


std::vector<SoupRecord> DeviceFile::soups(int64_t inStorePk)
{
  std::vector<SoupRecord> soups;
  std::string sql = std::string("SELECT ") + kSoupColumns + " FROM ZSOUP" + (inStorePk ? " WHERE ZSTORE = ?" : "") + " ORDER BY ZNAME";
  Stmt s(mDb, sql.c_str());
  if (inStorePk)
    s.bind(1, inStorePk);
  while (s.step())
    soups.push_back(ReadSoup(s));
  return soups;
}


std::optional<SoupRecord> DeviceFile::soup(int64_t inPk)
{
  Stmt s(mDb, (std::string("SELECT ") + kSoupColumns + " FROM ZSOUP WHERE Z_PK = ?").c_str());
  s.bind(1, inPk);
  if (!s.step())
    return std::nullopt;
  return ReadSoup(s);
}


std::optional<SoupRecord> DeviceFile::findSoup(int64_t inStorePk, const std::string & inName)
{
  Stmt s(mDb, (std::string("SELECT ") + kSoupColumns + " FROM ZSOUP WHERE ZSTORE = ? AND ZNAME = ? ORDER BY Z_PK LIMIT 1").c_str());
  s.bind(1, inStorePk).bind(2, inName);
  if (!s.step())
    return std::nullopt;
  return ReadSoup(s);
}


std::optional<AppRecord> DeviceFile::findApp(const std::string & inName)
{
  Stmt s(mDb, "SELECT Z_PK, ZNAME FROM ZAPP WHERE ZNAME = ? ORDER BY Z_PK LIMIT 1");
  s.bind(1, inName);
  if (!s.step())
    return std::nullopt;
  return AppRecord{ s.i64(0), s.text(1) };
}


std::vector<AppRecord> DeviceFile::storeApps(int64_t inStorePk, bool inWithPackages)
{
  std::vector<AppRecord> apps;
  std::string sql = std::string("SELECT Z_PK, ZNAME FROM ZAPP WHERE Z_PK IN (SELECT ZAPP FROM ZSOUP WHERE ZSTORE = ?)")
                  + (inWithPackages ? "" : " AND ZNAME != 'Packages'") + " ORDER BY ZNAME";
  Stmt s(mDb, sql.c_str());
  s.bind(1, inStorePk);
  while (s.step())
    apps.push_back(AppRecord{ s.i64(0), s.text(1) });
  return apps;
}


std::vector<EntryRecord> DeviceFile::storePackages(int64_t inStorePk)
{
  std::vector<EntryRecord> entries;
  Stmt s(mDb, ("SELECT " + EntryColumns(false) + " FROM ZENTRY WHERE ZSOUP IN (SELECT ZSOUP.Z_PK FROM ZSOUP JOIN ZAPP "
               "ON ZSOUP.ZAPP = ZAPP.Z_PK WHERE ZSOUP.ZSTORE = ? AND ZAPP.ZNAME = 'Packages') ORDER BY ZTITLE").c_str());
  s.bind(1, inStorePk);
  while (s.step())
    entries.push_back(ReadEntry(s, false));
  return entries;
}


std::vector<EntryRecord> DeviceFile::entries(int64_t inSoupPk, bool inWithData)
{
  std::vector<EntryRecord> entries;
  Stmt s(mDb, ("SELECT " + EntryColumns(inWithData) + " FROM ZENTRY WHERE ZSOUP = ? ORDER BY ZUNIQUEID").c_str());
  s.bind(1, inSoupPk);
  while (s.step())
    entries.push_back(ReadEntry(s, inWithData));
  return entries;
}


std::optional<EntryRecord> DeviceFile::entry(int64_t inPk, bool inWithData)
{
  Stmt s(mDb, ("SELECT " + EntryColumns(inWithData) + " FROM ZENTRY WHERE Z_PK = ?").c_str());
  s.bind(1, inPk);
  if (!s.step())
    return std::nullopt;
  return ReadEntry(s, inWithData);
}


std::optional<EntryRecord> DeviceFile::findEntry(int64_t inSoupPk, int64_t inUniqueId, bool inWithData)
{
  Stmt s(mDb, ("SELECT " + EntryColumns(inWithData) + " FROM ZENTRY WHERE ZSOUP = ? AND ZUNIQUEID = ? ORDER BY Z_PK LIMIT 1").c_str());
  s.bind(1, inSoupPk).bind(2, inUniqueId);
  if (!s.step())
    return std::nullopt;
  return ReadEntry(s, inWithData);
}


size_t DeviceFile::entryCount(int64_t inSoupPk)
{
  Stmt s(mDb, "SELECT COUNT(*) FROM ZENTRY WHERE ZSOUP = ?");
  s.bind(1, inSoupPk);
  return s.step() ? (size_t)s.i64(0) : 0;
}


size_t DeviceFile::rowCount(const char * inTable)
{
  Stmt s(mDb, (std::string("SELECT COUNT(*) FROM ") + inTable).c_str());
  return s.step() ? (size_t)s.i64(0) : 0;
}


/*-- writing -----------------------------------------------------------------*/

int DeviceFile::entityNumber(const std::string & inEntity) const
{
  auto found = mEntities.find(inEntity);
  if (found == mEntities.end())
    throw ModelError("no entity " + inEntity);
  return found->second;
}


int64_t DeviceFile::nextPk(const std::string & inEntity)
{
  if (mReadOnly)
    throw ModelError("the file is open for reading only");
  int entity = entityNumber(inEntity);
  Stmt max(mDb, "SELECT Z_MAX FROM Z_PRIMARYKEY WHERE Z_ENT = ?");
  max.bind(1, (int64_t)entity);
  int64_t pk = (max.step() ? max.i64(0) : 0) + 1;
  Stmt update(mDb, "UPDATE Z_PRIMARYKEY SET Z_MAX = ? WHERE Z_ENT = ?");
  update.bind(1, pk).bind(2, (int64_t)entity).run();
  return pk;
}


void DeviceFile::insert(DeviceRecord & ioDevice)
{
  Savepoint savepoint(mDb);
  int64_t pk = nextPk("Device");
  Stmt s(mDb, "INSERT INTO ZDEVICE (Z_PK, Z_ENT, Z_OPT, ZNAME, ZINFO, ZOSINFO, ZPROCESSOR, ZMANUFACTUREDATE, ZBACKUPDATE, "
              "ZBACKUPTIME, ZBACKUPERROR, ZSYNCMODE, ZUSER) VALUES (?, ?, 1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
  s.bind(1, pk).bind(2, (int64_t)entityNumber("Device")).bind(3, ioDevice.name).bind(4, ioDevice.info)
   .bind(5, ioDevice.osInfo).bind(6, ioDevice.processor).bind(7, ioDevice.manufactureDate).bind(8, ioDevice.backupDate)
   .bind(9, ioDevice.backupTime).bind(10, ioDevice.backupError).bind(11, ioDevice.syncMode).bindRef(12, ioDevice.userPk).run();
  savepoint.release();
  ioDevice.pk = pk;
}


void DeviceFile::update(const DeviceRecord & inDevice)
{
  if (mReadOnly)
    throw ModelError("the file is open for reading only");
  Stmt s(mDb, "UPDATE ZDEVICE SET Z_OPT = Z_OPT + 1, ZNAME = ?, ZINFO = ?, ZOSINFO = ?, ZPROCESSOR = ?, ZMANUFACTUREDATE = ?, "
              "ZBACKUPDATE = ?, ZBACKUPTIME = ?, ZBACKUPERROR = ?, ZSYNCMODE = ?, ZUSER = ? WHERE Z_PK = ?");
  s.bind(1, inDevice.name).bind(2, inDevice.info).bind(3, inDevice.osInfo).bind(4, inDevice.processor)
   .bind(5, inDevice.manufactureDate).bind(6, inDevice.backupDate).bind(7, inDevice.backupTime).bind(8, inDevice.backupError)
   .bind(9, inDevice.syncMode).bindRef(10, inDevice.userPk).bind(11, inDevice.pk).run();
}


void DeviceFile::insert(UserInfoRecord & ioUserInfo)
{
  Savepoint savepoint(mDb);
  int64_t pk = nextPk("UserInfo");
  Stmt s(mDb, "INSERT INTO ZUSERINFO (Z_PK, Z_ENT, Z_OPT, ZDEVICE, ZFOLDERS, ZFONT) VALUES (?, ?, 1, ?, ?, ?)");
  s.bind(1, pk).bind(2, (int64_t)entityNumber("UserInfo")).bindRef(3, ioUserInfo.devicePk)
   .bind(4, ioUserInfo.folders).bind(5, ioUserInfo.font).run();
  savepoint.release();
  ioUserInfo.pk = pk;
}


void DeviceFile::update(const UserInfoRecord & inUserInfo)
{
  if (mReadOnly)
    throw ModelError("the file is open for reading only");
  Stmt s(mDb, "UPDATE ZUSERINFO SET Z_OPT = Z_OPT + 1, ZDEVICE = ?, ZFOLDERS = ?, ZFONT = ? WHERE Z_PK = ?");
  s.bindRef(1, inUserInfo.devicePk).bind(2, inUserInfo.folders).bind(3, inUserInfo.font).bind(4, inUserInfo.pk).run();
}


void DeviceFile::insert(StoreRecord & ioStore)
{
  Savepoint savepoint(mDb);
  int64_t pk = nextPk("Store");
  Stmt s(mDb, "INSERT INTO ZSTORE (Z_PK, Z_ENT, Z_OPT, ZDEVICE, ZNAME, ZKIND, ZSTOREPASSWORD, ZSIGNATURE, ZSTOREVERSION, "
              "ZTOTALSIZE, ZUSEDSIZE, ZREADONLY, ZDEFAULTSTORE) VALUES (?, ?, 1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
  s.bind(1, pk).bind(2, (int64_t)entityNumber("Store")).bindRef(3, ioStore.devicePk).bind(4, ioStore.name)
   .bind(5, ioStore.kind).bind(6, ioStore.password).bind(7, ioStore.signature).bind(8, ioStore.storeVersion)
   .bind(9, ioStore.totalSize).bind(10, ioStore.usedSize).bindBool(11, ioStore.readOnly).bindBool(12, ioStore.defaultStore).run();
  savepoint.release();
  ioStore.pk = pk;
}


void DeviceFile::update(const StoreRecord & inStore)
{
  if (mReadOnly)
    throw ModelError("the file is open for reading only");
  Stmt s(mDb, "UPDATE ZSTORE SET Z_OPT = Z_OPT + 1, ZDEVICE = ?, ZNAME = ?, ZKIND = ?, ZSTOREPASSWORD = ?, ZSIGNATURE = ?, "
              "ZSTOREVERSION = ?, ZTOTALSIZE = ?, ZUSEDSIZE = ?, ZREADONLY = ?, ZDEFAULTSTORE = ? WHERE Z_PK = ?");
  s.bindRef(1, inStore.devicePk).bind(2, inStore.name).bind(3, inStore.kind).bind(4, inStore.password)
   .bind(5, inStore.signature).bind(6, inStore.storeVersion).bind(7, inStore.totalSize).bind(8, inStore.usedSize)
   .bindBool(9, inStore.readOnly).bindBool(10, inStore.defaultStore).bind(11, inStore.pk).run();
}


void DeviceFile::insert(AppRecord & ioApp)
{
  Savepoint savepoint(mDb);
  int64_t pk = nextPk("App");
  Stmt s(mDb, "INSERT INTO ZAPP (Z_PK, Z_ENT, Z_OPT, ZNAME) VALUES (?, ?, 1, ?)");
  s.bind(1, pk).bind(2, (int64_t)entityNumber("App")).bind(3, ioApp.name).run();
  savepoint.release();
  ioApp.pk = pk;
}


void DeviceFile::update(const AppRecord & inApp)
{
  if (mReadOnly)
    throw ModelError("the file is open for reading only");
  Stmt s(mDb, "UPDATE ZAPP SET Z_OPT = Z_OPT + 1, ZNAME = ? WHERE Z_PK = ?");
  s.bind(1, inApp.name).bind(2, inApp.pk).run();
}


void DeviceFile::insert(SoupRecord & ioSoup)
{
  Savepoint savepoint(mDb);
  int64_t pk = nextPk("Soup");
  Stmt s(mDb, "INSERT INTO ZSOUP (Z_PK, Z_ENT, Z_OPT, ZSTORE, ZAPP, ZNAME, ZDESCR, ZAPPNAME, ZINDEXES, ZINFO, ZSIGNATURE, "
              "ZLASTBACKUPID, ZLASTBACKUPTIME, ZLASTIMPORTID, ZPREVSYNCHTIME, ZPREVSYNCHIDDATA) "
              "VALUES (?, ?, 1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
  s.bind(1, pk).bind(2, (int64_t)entityNumber("Soup")).bindRef(3, ioSoup.storePk).bindRef(4, ioSoup.appPk)
   .bind(5, ioSoup.name).bind(6, ioSoup.descr).bind(7, ioSoup.appName).bind(8, ioSoup.indexes).bind(9, ioSoup.info)
   .bind(10, ioSoup.signature).bind(11, ioSoup.lastBackupId).bind(12, ioSoup.lastBackupTime).bind(13, ioSoup.lastImportId)
   .bind(14, ioSoup.prevSynchTime).bind(15, ioSoup.prevSynchIdData).run();
  savepoint.release();
  ioSoup.pk = pk;
}


void DeviceFile::update(const SoupRecord & inSoup)
{
  if (mReadOnly)
    throw ModelError("the file is open for reading only");
  Stmt s(mDb, "UPDATE ZSOUP SET Z_OPT = Z_OPT + 1, ZSTORE = ?, ZAPP = ?, ZNAME = ?, ZDESCR = ?, ZAPPNAME = ?, ZINDEXES = ?, "
              "ZINFO = ?, ZSIGNATURE = ?, ZLASTBACKUPID = ?, ZLASTBACKUPTIME = ?, ZLASTIMPORTID = ?, ZPREVSYNCHTIME = ?, "
              "ZPREVSYNCHIDDATA = ? WHERE Z_PK = ?");
  s.bindRef(1, inSoup.storePk).bindRef(2, inSoup.appPk).bind(3, inSoup.name).bind(4, inSoup.descr).bind(5, inSoup.appName)
   .bind(6, inSoup.indexes).bind(7, inSoup.info).bind(8, inSoup.signature).bind(9, inSoup.lastBackupId)
   .bind(10, inSoup.lastBackupTime).bind(11, inSoup.lastImportId).bind(12, inSoup.prevSynchTime)
   .bind(13, inSoup.prevSynchIdData).bind(14, inSoup.pk).run();
}


void DeviceFile::insert(EntryRecord & ioEntry)
{
  Savepoint savepoint(mDb);
  int64_t pk = nextPk("Entry");
  Stmt s(mDb, "INSERT INTO ZENTRY (Z_PK, Z_ENT, Z_OPT, ZSOUP, ZUNIQUEID, ZMODTIME, ZREFCLASS, ZTITLE, ZREFDATA) "
              "VALUES (?, ?, 1, ?, ?, ?, ?, ?, ?)");
  s.bind(1, pk).bind(2, (int64_t)entityNumber("Entry")).bindRef(3, ioEntry.soupPk).bind(4, ioEntry.uniqueId)
   .bind(5, ioEntry.modTime).bind(6, ioEntry.refClass).bind(7, ioEntry.title).bind(8, ioEntry.refData).run();
  savepoint.release();
  ioEntry.pk = pk;
}


void DeviceFile::update(const EntryRecord & inEntry)
{
  if (mReadOnly)
    throw ModelError("the file is open for reading only");
  Stmt s(mDb, "UPDATE ZENTRY SET Z_OPT = Z_OPT + 1, ZSOUP = ?, ZUNIQUEID = ?, ZMODTIME = ?, ZREFCLASS = ?, ZTITLE = ?, "
              "ZREFDATA = ? WHERE Z_PK = ?");
  s.bindRef(1, inEntry.soupPk).bind(2, inEntry.uniqueId).bind(3, inEntry.modTime).bind(4, inEntry.refClass)
   .bind(5, inEntry.title).bind(6, inEntry.refData).bind(7, inEntry.pk).run();
}


void DeviceFile::deleteEntry(int64_t inPk)
{
  if (mReadOnly)
    throw ModelError("the file is open for reading only");
  Stmt s(mDb, "DELETE FROM ZENTRY WHERE Z_PK = ?");
  s.bind(1, inPk).run();
}


void DeviceFile::deleteSoup(int64_t inPk)
{
  if (mReadOnly)
    throw ModelError("the file is open for reading only");
  Savepoint savepoint(mDb);
  Stmt entries(mDb, "DELETE FROM ZENTRY WHERE ZSOUP = ?");
  entries.bind(1, inPk).run();
  Stmt soup(mDb, "DELETE FROM ZSOUP WHERE Z_PK = ?");
  soup.bind(1, inPk).run();
  savepoint.release();
}


void DeviceFile::deleteStore(int64_t inPk)
{
  if (mReadOnly)
    throw ModelError("the file is open for reading only");
  Savepoint savepoint(mDb);
  for (const SoupRecord & soup : soups(inPk))
    deleteSoup(soup.pk);
  Stmt store(mDb, "DELETE FROM ZSTORE WHERE Z_PK = ?");
  store.bind(1, inPk).run();
  savepoint.release();
}


size_t DeviceFile::cropSoup(int64_t inSoupPk, const std::vector<int64_t> & inKeep)
{
  if (mReadOnly)
    throw ModelError("the file is open for reading only");
  std::set<int64_t> keep(inKeep.begin(), inKeep.end());
  std::vector<int64_t> gone;
  {
    Stmt s(mDb, "SELECT Z_PK, ZUNIQUEID FROM ZENTRY WHERE ZSOUP = ? AND ZUNIQUEID < ?");
    s.bind(1, inSoupPk).bind(2, kImportIdBase);
    while (s.step())
      if (!keep.count(s.i64(1)))
        gone.push_back(s.i64(0));
  }
  Savepoint savepoint(mDb);
  for (int64_t pk : gone) {
    NCX_LOG_FULL("pruning entry %lld", (long long)pk);
    deleteEntry(pk);
  }
  savepoint.release();
  return gone.size();
}


/*-- transactions ------------------------------------------------------------*/

DeviceFile::Transaction::Transaction(DeviceFile & inFile) : mFile(inFile)
{
  mFile.exec("BEGIN IMMEDIATE");
}


DeviceFile::Transaction::~Transaction()
{
  if (!mDone)
    sqlite3_exec(mFile.mDb, "ROLLBACK", nullptr, nullptr, nullptr);
}


void DeviceFile::Transaction::commit()
{
  mFile.exec("COMMIT");
  mDone = true;
}

} // namespace ncx
