/*
 File: CoreDataModel.cc

 The Cocoa app's Core Data model, as far as SQLite sees it (CoreDataModel.h).
 */

#include "model/CoreDataModel.h"

#include <cstdio>
#include <random>

namespace ncx {
namespace coredata {

namespace {

// NSStoreModelVersionHashes (SHA-256 of each entity's description)
const uint8_t kAppHash[32] = { 0xE1, 0xE8, 0x7C, 0x37, 0x82, 0x54, 0xB3, 0x87, 0x4E, 0xB3, 0x02, 0xD5, 0x73, 0x17, 0x26, 0x3F,
                               0x11, 0x19, 0x9A, 0x16, 0xE2, 0x38, 0xC1, 0x2C, 0x14, 0xF5, 0xEB, 0x0D, 0xC4, 0x7B, 0x21, 0xA3 };
const uint8_t kDeviceHash[32] = { 0xB3, 0x88, 0xA2, 0x9C, 0x2E, 0x1D, 0x3E, 0xB7, 0xFA, 0x8E, 0xAC, 0x78, 0x80, 0x10, 0x97, 0x5E,
                                  0xCF, 0x75, 0x96, 0x94, 0x06, 0x91, 0xD9, 0x54, 0x3F, 0x0E, 0x21, 0x1F, 0xBF, 0xBA, 0xCA, 0xE2 };
const uint8_t kEntryHash[32] = { 0x14, 0x7D, 0x6C, 0x06, 0xBC, 0xB0, 0x86, 0xB4, 0x29, 0xA7, 0xB2, 0xF9, 0x24, 0xC4, 0x79, 0x39,
                                 0x68, 0xAC, 0x94, 0xB8, 0xD5, 0x65, 0x6E, 0x79, 0x36, 0x9C, 0x71, 0xC6, 0xBE, 0x5F, 0xE6, 0x7C };
const uint8_t kSoupHash[32] = { 0xFC, 0x13, 0x84, 0x6E, 0xDF, 0x9F, 0x20, 0x47, 0x55, 0x9D, 0xCC, 0x3D, 0xBB, 0xE1, 0xDA, 0x24,
                                0xB4, 0xCB, 0xA7, 0xC3, 0xF1, 0x44, 0x21, 0xDF, 0x36, 0xD0, 0xFA, 0x84, 0xEC, 0xBB, 0xAD, 0x46 };
const uint8_t kStoreHash[32] = { 0x64, 0x13, 0x81, 0x9E, 0x23, 0x5E, 0xF9, 0xB7, 0x3F, 0x9A, 0xC4, 0x10, 0x6E, 0x7A, 0x2B, 0xDA,
                                 0xFC, 0x92, 0xB4, 0xAD, 0xB5, 0x04, 0x71, 0xBA, 0x13, 0x8F, 0xE3, 0x85, 0xFE, 0x77, 0xF2, 0xAD };
const uint8_t kUserInfoHash[32] = { 0x83, 0xC8, 0xD1, 0x05, 0xFC, 0xCD, 0xCE, 0xBD, 0xDD, 0xEE, 0x73, 0x79, 0x2C, 0x84, 0x5C, 0x6A,
                                    0x4C, 0xE4, 0x9D, 0x26, 0x9E, 0xC3, 0x70, 0xFB, 0xBE, 0xDD, 0xA1, 0x57, 0xEE, 0x26, 0x2E, 0x51 };

const char * const kChecksum = "v8cNauKyq5LC67B7OGmxqxpUcqReqjZTIdmJPUIWYDU=";
const char * const kHashesDigest = "MDYRW5rLzQoZ6kwIXD79ZbPsplEUV6iHGPeZ0Q7/hShwX4QrF0YSPgmO4jCbN/aKufKd6pnTMBWWrQUhEiAuyQ==";

Plist Hash(const uint8_t (&inHash)[32])
{
  return Plist::Data(std::vector<uint8_t>(inHash, inHash + 32));
}

Plist Hashes()
{
  Plist hashes = Plist::Dict();
  hashes.set("App", Hash(kAppHash));
  hashes.set("Device", Hash(kDeviceHash));
  hashes.set("Entry", Hash(kEntryHash));
  hashes.set("Soup", Hash(kSoupHash));
  hashes.set("Store", Hash(kStoreHash));
  hashes.set("UserInfo", Hash(kUserInfoHash));
  return hashes;
}

} // namespace


const std::vector<std::string> & EntityNames()
{
  static const std::vector<std::string> names = { "App", "Device", "Entry", "Soup", "Store", "UserInfo" };
  return names;
}


const std::vector<std::string> & Schema()
{
  static const std::vector<std::string> schema = {
    "CREATE TABLE ZAPP ( Z_PK INTEGER PRIMARY KEY, Z_ENT INTEGER, Z_OPT INTEGER, ZNAME VARCHAR )",
    "CREATE TABLE ZDEVICE ( Z_PK INTEGER PRIMARY KEY, Z_ENT INTEGER, Z_OPT INTEGER, ZBACKUPERROR INTEGER, ZBACKUPTIME INTEGER, "
      "ZSYNCMODE INTEGER, ZUSER INTEGER, ZBACKUPDATE TIMESTAMP, ZMANUFACTUREDATE TIMESTAMP, ZOSINFO VARCHAR, ZNAME VARCHAR, "
      "ZPROCESSOR VARCHAR, ZINFO BLOB )",
    "CREATE TABLE ZENTRY ( Z_PK INTEGER PRIMARY KEY, Z_ENT INTEGER, Z_OPT INTEGER, ZUNIQUEID INTEGER, ZSOUP INTEGER, "
      "ZMODTIME TIMESTAMP, ZREFCLASS VARCHAR, ZTITLE VARCHAR, ZREFDATA BLOB )",
    "CREATE TABLE ZSOUP ( Z_PK INTEGER PRIMARY KEY, Z_ENT INTEGER, Z_OPT INTEGER, ZLASTBACKUPID INTEGER, ZLASTBACKUPTIME INTEGER, "
      "ZLASTIMPORTID INTEGER, ZSIGNATURE INTEGER, ZAPP INTEGER, ZSTORE INTEGER, ZPREVSYNCHTIME TIMESTAMP, ZAPPNAME VARCHAR, "
      "ZDESCR VARCHAR, ZNAME VARCHAR, ZINDEXES BLOB, ZINFO BLOB, ZPREVSYNCHIDDATA BLOB )",
    "CREATE TABLE ZSTORE ( Z_PK INTEGER PRIMARY KEY, Z_ENT INTEGER, Z_OPT INTEGER, ZDEFAULTSTORE INTEGER, ZREADONLY INTEGER, "
      "ZSIGNATURE INTEGER, ZSTOREVERSION INTEGER, ZTOTALSIZE INTEGER, ZUSEDSIZE INTEGER, ZDEVICE INTEGER, ZKIND VARCHAR, "
      "ZNAME VARCHAR, ZSTOREPASSWORD VARCHAR )",
    "CREATE TABLE ZUSERINFO ( Z_PK INTEGER PRIMARY KEY, Z_ENT INTEGER, Z_OPT INTEGER, ZDEVICE INTEGER, ZFOLDERS BLOB, ZFONT BLOB )",
    "CREATE INDEX ZDEVICE_ZUSER_INDEX ON ZDEVICE (ZUSER)",
    "CREATE INDEX ZENTRY_ZSOUP_INDEX ON ZENTRY (ZSOUP)",
    "CREATE INDEX ZSOUP_ZAPP_INDEX ON ZSOUP (ZAPP)",
    "CREATE INDEX ZSOUP_ZSTORE_INDEX ON ZSOUP (ZSTORE)",
    "CREATE INDEX ZSTORE_ZDEVICE_INDEX ON ZSTORE (ZDEVICE)",
    "CREATE INDEX ZUSERINFO_ZDEVICE_INDEX ON ZUSERINFO (ZDEVICE)",
    "CREATE TABLE Z_PRIMARYKEY (Z_ENT INTEGER PRIMARY KEY, Z_NAME VARCHAR, Z_SUPER INTEGER, Z_MAX INTEGER)",
    "CREATE TABLE Z_METADATA (Z_VERSION INTEGER PRIMARY KEY, Z_UUID VARCHAR(255), Z_PLIST BLOB)",
    "CREATE TABLE Z_MODELCACHE (Z_CONTENT BLOB)"
  };
  return schema;
}


Plist MakeMetadata(const std::string & inNewtonId, const std::string & inNewtonName)
{
  Plist metadata = Plist::Dict();
  metadata.set("NSPersistenceFrameworkVersion", Plist::Int(1525));
  metadata.set("NSPersistenceMaximumFrameworkVersion", Plist::Int(1525));
  metadata.set("NSStoreModelVersionChecksumKey", Plist::String(kChecksum));
  metadata.set("NSStoreModelVersionHashes", Hashes());
  metadata.set("NSStoreModelVersionHashesDigest", Plist::String(kHashesDigest));
  metadata.set("NSStoreModelVersionHashesVersion", Plist::Int(3));
  metadata.set("NSStoreModelVersionIdentifiers", Plist::Array({ Plist::String("") }));
  metadata.set("NSStoreType", Plist::String("SQLite"));
  metadata.set("NewtonId", Plist::String(inNewtonId));
  metadata.set("NewtonName", Plist::String(inNewtonName));
  metadata.set("_NSAutoVacuumLevel", Plist::String("2"));
  return metadata;
}


bool IsThisModel(const Plist & inMetadata)
{
  const Plist * hashes = inMetadata.find("NSStoreModelVersionHashes");
  return hashes && *hashes == Hashes();
}


std::string MakeUUID()
{
  std::random_device random;
  uint8_t bytes[16];
  for (int i = 0; i < 16; i += 4) {
    uint32_t r = random();
    for (int k = 0; k < 4; ++k)
      bytes[i + k] = (uint8_t)(r >> (8 * k));
  }
  bytes[6] = (uint8_t)((bytes[6] & 0x0F) | 0x40);     // version 4
  bytes[8] = (uint8_t)((bytes[8] & 0x3F) | 0x80);     // variant
  char text[40];
  std::snprintf(text, sizeof(text), "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
                bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
  return text;
}

} // namespace coredata
} // namespace ncx
