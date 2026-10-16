/*
 File: BackupImport.cc

 Newton Connection 1 backups (BackupImport.h).
 */

#include "model/BackupImport.h"
#include "model/DeviceList.h"
#include "model/NewtonModel.h"
#include "base/Log.h"
#include "base/NewtonStrings.h"
#include "comms/DockRefs.h"
#include "platform/AppFolders.h"

#include "RSSymbols.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <set>

namespace ncx {

namespace {

const size_t kHeaderSize = 20;      // "backup0\0" and the SourceInfo

uint32_t WordAt(const Bytes & inData, size_t inOffset, bool inBigEndian)
{
  const uint8_t * p = inData.data() + inOffset;
  if (inBigEndian)
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}


bool LooksLikeMachineType(uint32_t inType)
{
  // as the Cocoa app had it: the gestalt machine types, or the MP 120/130's
  return (inType & 0xFFFF0000) == 0x10000000 || inType == 0x00726377;
}


/** The NSOF object at ioOffset; ioOffset after it. A ModelError if it
    isn't NSOF. */
Ref NextObject(const Bytes & inData, size_t & ioOffset, Bytes * outNSOF = nullptr)
{
  if (ioOffset >= inData.size())
    throw ModelError("the backup ends early");
  size_t end = ioOffset;
  RefVar obj(UnflattenBytes(inData, ioOffset, &end));
  if (end <= ioOffset)
    throw ModelError("the backup is damaged at byte " + std::to_string(ioOffset));
  if (outNSOF)
    outNSOF->assign(inData.begin() + (ptrdiff_t)ioOffset, inData.begin() + (ptrdiff_t)end);
  ioOffset = end;
  return obj;
}


void ReadBackup(DeviceFile & ioFile, const Bytes & inData, const std::string & inName)
{
  // the device: all there is about it is the SourceInfo (little-endian,
  // unless a PowerPC Mac wrote it)
  bool bigEndian = !LooksLikeMachineType(WordAt(inData, 16, false));
  NewtonInfo info;
  info.fNOSVersion = WordAt(inData, 8, bigEndian);
  info.fManufacturer = WordAt(inData, 12, bigEndian);
  info.fMachineType = WordAt(inData, 16, bigEndian);
  DeviceRecord device = SetDevice(ioFile, inName, info);

  size_t offset = kHeaderSize;
  RefVar storeFrame(NextObject(inData, offset));
  if (!IsFrame(storeFrame))
    throw ModelError("the backup has no store");
  StoreRecord store = AddStore(ioFile, device.pk, storeFrame);
  if (!store.password) {
    store.password = std::string();
    ioFile.update(store);
  }

  // the store's soups; those in no app have no data
  std::vector<std::string> orphans;
  RefVar allSoups(GetFrameSlot(storeFrame, MakeSymbol("soups")));
  if (IsArray(allSoups))
    for (ArrayIndex i = 0, count = Length(allSoups); i < count; ++i) {
      RefVar soupName(GetArraySlot(allSoups, i));
      if (IsString(soupName))
        orphans.push_back(UTF8FromString(soupName));
    }

  RefVar apps(GetFrameSlot(storeFrame, MakeSymbol("apps")));
  for (ArrayIndex i = 0, appCount = IsArray(apps) ? Length(apps) : 0; i < appCount; ++i) {
    RefVar app(GetArraySlot(apps, i));
    AppRecord appRecord;
    appRecord.name = UTF8FromString(GetFrameSlot(app, SYMA(name)));
    ioFile.insert(appRecord);      // each app its own, as the Cocoa app made them
    bool isPackages = appRecord.name == "Packages";
    RefVar appSoups(GetFrameSlot(app, MakeSymbol("soups")));
    for (ArrayIndex j = 0, soupCount = IsArray(appSoups) ? Length(appSoups) : 0; j < soupCount; ++j) {
      std::string soupName = UTF8FromString(GetArraySlot(appSoups, j));
      Bytes indexesNSOF, infoNSOF;
      RefVar indexes(NextObject(inData, offset, &indexesNSOF));
      RefVar soupInfo(NextObject(inData, offset, &infoNSOF));
      SoupRecord soup = MakeSoup(store.pk, soupName, 0, indexes, soupInfo);
      // the bytes as they were in the file
      soup.indexes = indexesNSOF;
      soup.info = infoNSOF;
      soup.appPk = appRecord.pk;
      ioFile.insert(soup);
      for (;;) {
        Bytes entryNSOF;
        RefVar entry(NextObject(inData, offset, &entryNSOF));
        if (ISNIL(entry))
          break;
        if (!IsFrame(entry))
          throw ModelError("the backup has a soup entry that isn't a frame");
        if (isPackages) {
          // only the packages themselves
          if (!FrameHasSlot(entry, MakeSymbol("pkgRef")))
            continue;
          SetFrameSlot(entry, SYMA(class), MakeSymbol("*package*"));
          entryNSOF = FlattenToBytes(entry);
        }
        AddEntry(ioFile, soup, entry, entryNSOF);
      }
      for (auto it = orphans.begin(); it != orphans.end(); ++it)
        if (*it == soupName) {
          orphans.erase(it);
          break;
        }
    }
  }

  for (const std::string & soupName : orphans) {
    SoupRecord soup;
    soup.storePk = store.pk;
    soup.name = soupName;
    soup.descr = "Orphan soup - no definition.";
    soup.appName = "--";
    soup.lastImportId = kImportIdBase;
    ioFile.insert(soup);
  }
}

} // namespace


bool IsBackup(const Bytes & inData)
{
  return inData.size() >= kHeaderSize && std::memcmp(inData.data(), "backup", 6) == 0;
}


std::unique_ptr<DeviceFile> ImportBackup(const Bytes & inData, const std::string & inName, std::string & outError)
{
  if (!IsBackup(inData)) {
    outError = "not a Newton Connection backup";
    return nullptr;
  }
  std::unique_ptr<DeviceFile> file = DeviceFile::CreateInMemory(inName, outError);
  if (!file)
    return nullptr;
  try {
    DeviceFile::Transaction transaction(*file);
    ReadBackup(*file, inData, inName);
    transaction.commit();
  } catch (const ModelError & e) {
    outError = e.what();
    Log(kLogMinimum, "backup: %s", outError.c_str());
    return nullptr;
  }
  return file;
}


std::unique_ptr<DeviceFile> ImportBackup(const std::filesystem::path & inPath, std::string & outError)
{
  std::ifstream in(inPath, std::ios::binary);
  if (!in) {
    outError = "can't open " + UTF8FromPath(inPath);
    return nullptr;
  }
  Bytes data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return ImportBackup(data, UTF8FromPath(inPath.stem()), outError);
}

} // namespace ncx
