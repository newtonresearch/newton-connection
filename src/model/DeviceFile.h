/*
 File: DeviceFile.h

 A .newtondevice file (PORTING.md 4.2): a Newton's backup, an SQLite file
 in the Cocoa app's Core Data schema (CoreDataModel.h), read and written
 without Core Data. It keeps Core Data's bookkeeping, so the macOS Core Data
 NCX still opens what this writes:
   - a new row's Z_PK is its entity's Z_PRIMARYKEY.Z_MAX + 1 (and Z_MAX
     follows); Z_ENT is the entity's number; Z_OPT starts at 1 and goes up
     with each change
   - Z_METADATA stays as found, but for NCX's NewtonId and NewtonName
 A transaction replaces Core Data's save:. The model's delete rules only
 cut the links, so deleting a soup deletes its entries here, deleting a
 store its soups.

 Failures throw ModelError. One thread at a time (SQLite serializes).
 */

#ifndef NCX_MODEL_DEVICEFILE_H
#define NCX_MODEL_DEVICEFILE_H

#include "model/Plist.h"
#include "model/Records.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

struct sqlite3;

namespace ncx {

struct ModelError : std::runtime_error
{
  explicit ModelError(const std::string & inWhat) : std::runtime_error(inWhat) { }
};

class DeviceFile
{
public:
  enum Mode { kReadOnly, kReadWrite };

  /** An existing file. nullptr and why if it can't be opened, or isn't a
      .newtondevice file. */
  static std::unique_ptr<DeviceFile> Open(const std::filesystem::path & inPath, Mode inMode, std::string & outError);

  /** A new file, empty but for the bookkeeping; not if one is there. */
  static std::unique_ptr<DeviceFile> Create(const std::filesystem::path & inPath, const std::string & inNewtonId,
                                            const std::string & inNewtonName, std::string & outError);

  /** One in memory only (a legacy backup's contents, BackupImport.h). */
  static std::unique_ptr<DeviceFile> CreateInMemory(const std::string & inNewtonName, std::string & outError);

  ~DeviceFile();
  DeviceFile(const DeviceFile &) = delete;
  DeviceFile & operator=(const DeviceFile &) = delete;

  /** A copy as a new file, in WAL mode (an in-memory file saved); not if
      one is there. This one stays as it is. */
  void saveAs(const std::filesystem::path & inPath);

  const std::filesystem::path & path() const { return mPath; }
  bool readOnly() const { return mReadOnly; }

  /*-- metadata --*/

  const Plist & metadata() const { return mMetadata; }
  std::string newtonId() const;
  std::string newtonName() const;
  /** NewtonId and NewtonName in Z_METADATA (what the device list shows). */
  void setIdentity(const std::string & inNewtonId, const std::string & inNewtonName);

  /*-- reading --*/

  /** The device (a file has one, or none yet). */
  std::optional<DeviceRecord> device();
  std::optional<UserInfoRecord> userInfo(int64_t inPk);
  std::vector<StoreRecord> stores();
  std::vector<AppRecord> apps();
  /** The soups of a store (0: all of them). */
  std::vector<SoupRecord> soups(int64_t inStorePk = 0);
  std::optional<SoupRecord> soup(int64_t inPk);
  std::optional<SoupRecord> findSoup(int64_t inStorePk, const std::string & inName);
  std::optional<AppRecord> findApp(const std::string & inName);
  /** The apps with soups on a store, by name; Packages too if asked (NCStore
      apps:). */
  std::vector<AppRecord> storeApps(int64_t inStorePk, bool inWithPackages);
  /** The packages backed up from a store (the Packages app's entries), by
      title, without their data (NCStore pkgs). */
  std::vector<EntryRecord> storePackages(int64_t inStorePk);
  /** A soup's entries by _uniqueId; without their data unless asked. */
  std::vector<EntryRecord> entries(int64_t inSoupPk, bool inWithData = false);
  std::optional<EntryRecord> entry(int64_t inPk, bool inWithData = true);
  std::optional<EntryRecord> findEntry(int64_t inSoupPk, int64_t inUniqueId, bool inWithData = true);
  size_t entryCount(int64_t inSoupPk);
  /** Rows in a table ("ZENTRY", ...). */
  size_t rowCount(const char * inTable);

  /*-- writing (insert sets the record's pk) --*/

  void insert(DeviceRecord & ioDevice);
  void update(const DeviceRecord & inDevice);
  void insert(UserInfoRecord & ioUserInfo);
  void update(const UserInfoRecord & inUserInfo);
  void insert(StoreRecord & ioStore);
  void update(const StoreRecord & inStore);
  void insert(AppRecord & ioApp);
  void update(const AppRecord & inApp);
  void insert(SoupRecord & ioSoup);
  void update(const SoupRecord & inSoup);
  void insert(EntryRecord & ioEntry);
  void update(const EntryRecord & inEntry);

  void deleteEntry(int64_t inPk);
  /** With its entries. */
  void deleteSoup(int64_t inPk);
  /** With its soups and their entries. */
  void deleteStore(int64_t inPk);
  /** The soup's entries (but imported ones, kImportIdBase and up) whose
      _uniqueId isn't in inKeep (NCSoup's cropTo:); how many went. */
  size_t cropSoup(int64_t inSoupPk, const std::vector<int64_t> & inKeep);

  /*-- transactions --*/

  class Transaction
  {
  public:
    explicit Transaction(DeviceFile & inFile);
    ~Transaction();           // rolls back unless committed
    void commit();
  private:
    DeviceFile & mFile;
    bool mDone = false;
  };

private:
  DeviceFile() { }
  void exec(const char * inSQL);
  bool load(std::string & outError);
  int64_t nextPk(const std::string & inEntity);
  int entityNumber(const std::string & inEntity) const;
  void writeMetadata();

  sqlite3 * mDb = nullptr;
  std::filesystem::path mPath;
  bool mReadOnly = false;
  std::map<std::string, int> mEntities;
  Plist mMetadata;
};

} // namespace ncx

#endif // NCX_MODEL_DEVICEFILE_H
