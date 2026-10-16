/*
 File: Records.h

 The rows of a .newtondevice file (PORTING.md 4.1): the Cocoa app's Core
 Data entities as plain structs (NCDevice, NCUserInfo, NCStore, NCApp,
 NCSoup, NCEntry), one field per column. pk is the row's Z_PK (0: not in a
 file yet); a reference to another row is its pk (0: none). Times are Core
 Data's (seconds since 2001, base/Dates.h), except the Newton times the
 Cocoa app kept as numbers (backupTime, lastBackupTime). What the UI shows
 (images, icons, status text) is the UI's.
 */

#ifndef NCX_MODEL_RECORDS_H
#define NCX_MODEL_RECORDS_H

#include "comms/DockProtocol.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ncx {

typedef std::vector<uint8_t> Bytes;

/** Soup entries imported on the desktop get _uniqueIds from here up until
    they are synced (the Cocoa app's kImportIdBase). */
inline constexpr int64_t kImportIdBase = 1070000000;

struct DeviceRecord
{
  int64_t pk = 0;
  std::string name;                       // the owner's name
  Bytes info;                             // NewtonInfo, little-endian (NewtonInfoOf)
  std::optional<std::string> osInfo;      // "2.1 (717006)-0"
  std::optional<std::string> processor;   // "99.5 MHz  StrongARM"
  std::optional<double> manufactureDate;
  std::optional<double> backupDate;       // the last backup (Core Data time)
  std::optional<int64_t> backupTime;      // ... in Newton time (NewtonOS 1 sync)
  std::optional<int64_t> backupError;
  int64_t syncMode = 0;
  int64_t userPk = 0;
};

struct UserInfoRecord
{
  int64_t pk = 0;
  int64_t devicePk = 0;
  std::optional<Bytes> folders;           // keyed archives (NSDictionary, NSFont):
  std::optional<Bytes> font;              // kept as they are
};

struct StoreRecord
{
  int64_t pk = 0;
  int64_t devicePk = 0;
  std::string name;
  std::string kind;                       // "Internal", ...
  std::optional<std::string> password;
  int64_t signature = 0;
  int64_t storeVersion = 0;
  int64_t totalSize = 0;
  int64_t usedSize = 0;
  bool readOnly = false;
  bool defaultStore = false;
};

struct AppRecord
{
  int64_t pk = 0;
  std::string name;                       // "Packages" for the packages
};

struct SoupRecord
{
  int64_t pk = 0;
  int64_t storePk = 0;
  int64_t appPk = 0;
  std::string name;
  std::string descr;                      // the soupDef's userDescr
  std::string appName;                    // its ownerAppName (or ownerApp)
  Bytes indexes;                          // NSOF
  Bytes info;                             // NSOF
  int64_t signature = 0;
  int64_t lastBackupId = 0;               // the highest _uniqueId backed up
  int64_t lastBackupTime = 0;             // Newton time (info's NCKLastBackupTime)
  std::optional<int64_t> lastImportId;    // the last kImportIdBase id given out
  std::optional<double> prevSynchTime;
  std::optional<Bytes> prevSynchIdData;   // a keyed archive (NSIndexSet): kept as it is
};

struct EntryRecord
{
  int64_t pk = 0;
  int64_t soupPk = 0;
  int64_t uniqueId = 0;
  double modTime = 0;
  std::optional<std::string> refClass;    // the entry's class, or "*package*"
  std::optional<std::string> title;
  Bytes refData;                          // the entry, NSOF (left out by DeviceFile::entries)
};

/** A store is the Newton's internal one (by its kind, NCStore isInternal). */
inline bool IsInternal(const StoreRecord & inStore) { return inStore.kind == "Internal"; }
/** Internal first, then by name (NCDocument stores). */
void SortStores(std::vector<StoreRecord> & ioStores);

/*-- the Newton's info block, and what the Cocoa app made of it --------------*/

/** ZDEVICE.ZINFO as a NewtonInfo (the Cocoa app kept the struct as it was in
    memory: little-endian). */
NewtonInfo NewtonInfoOf(const DeviceRecord & inDevice);
void SetNewtonInfo(DeviceRecord & ioDevice, const NewtonInfo & inInfo);

/** The device's id, as file names and Z_METADATA's NewtonId have it: its
    serial number "0000-4E65-7774-6F6E", or its NewtonID in decimal when it
    has none. */
std::string DeviceId(const NewtonInfo & inInfo);

std::string ManufacturerName(const NewtonInfo & inInfo);    // "Apple", "Sharp", "Unknown"
std::string MachineTypeName(const NewtonInfo & inInfo);     // "MessagePad 2000", "eMate", ...
std::string ROMVersionText(const NewtonInfo & inInfo);      // "2.1"
std::string SerialNumberText(const NewtonInfo & inInfo);    // "0000-4E65-7774-6F6E"
/** NewtonOS 1.x data (no RAM size in the info). */
bool Is1xData(const NewtonInfo & inInfo);

} // namespace ncx

#endif // NCX_MODEL_RECORDS_H
