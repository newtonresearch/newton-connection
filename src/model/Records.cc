/*
 File: Records.cc

 The Newton's info block in a device record (Records.h).
 */

#include "model/Records.h"

#include <algorithm>
#include <cstdio>

namespace ncx {

namespace {

// gestalt values (NewtonGestalt.h)
const uint32_t kManufacturerApple = 0x01000000;
const uint32_t kManufacturerSharp = 0x10000100;
const uint32_t kMachineTypeLindy = 0x00726377;      // MessagePad 120/130
const uint32_t kMachineTypeQ = 0x10003000;          // MessagePad 2000/2100
const uint32_t kMachineTypeK = 0x10004000;          // eMate 300

const size_t kInfoWords = sizeof(NewtonInfo) / 4;

} // namespace


void SortStores(std::vector<StoreRecord> & ioStores)
{
  std::stable_sort(ioStores.begin(), ioStores.end(), [](const StoreRecord & a, const StoreRecord & b) {
    if ((a.name == "Internal") != (b.name == "Internal"))
      return a.name == "Internal";
    return a.name < b.name;
  });
}


NewtonInfo NewtonInfoOf(const DeviceRecord & inDevice)
{
  NewtonInfo info;
  uint32_t * words = reinterpret_cast<uint32_t *>(&info);
  for (size_t i = 0; i < kInfoWords && i * 4 + 4 <= inDevice.info.size(); ++i) {
    const uint8_t * p = inDevice.info.data() + i * 4;
    words[i] = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
  }
  return info;
}


void SetNewtonInfo(DeviceRecord & ioDevice, const NewtonInfo & inInfo)
{
  const uint32_t * words = reinterpret_cast<const uint32_t *>(&inInfo);
  ioDevice.info.clear();
  for (size_t i = 0; i < kInfoWords; ++i)
    for (int shift = 0; shift < 32; shift += 8)
      ioDevice.info.push_back((uint8_t)(words[i] >> shift));
}


std::string SerialNumberText(const NewtonInfo & inInfo)
{
  char text[24];
  std::snprintf(text, sizeof(text), "%04X-%04X-%04X-%04X", inInfo.fSerialNumber[0] >> 16, inInfo.fSerialNumber[0] & 0xFFFF,
                inInfo.fSerialNumber[1] >> 16, inInfo.fSerialNumber[1] & 0xFFFF);
  return text;
}


std::string DeviceId(const NewtonInfo & inInfo)
{
  std::string serial = SerialNumberText(inInfo);
  if (serial != "0000-0000-0000-0000")
    return serial;
  return std::to_string(inInfo.fNewtonID);
}


std::string ManufacturerName(const NewtonInfo & inInfo)
{
  switch (inInfo.fManufacturer) {
    case kManufacturerApple: return "Apple";
    case kManufacturerSharp: return "Sharp";
  }
  return "Unknown";
}


std::string MachineTypeName(const NewtonInfo & inInfo)
{
  switch (inInfo.fMachineType) {
    case kMachineTypeLindy: return "MessagePad 130";
    case kMachineTypeQ: return "MessagePad 2000";
    case kMachineTypeK: return "eMate";
  }
  return "MessagePad";
}


std::string ROMVersionText(const NewtonInfo & inInfo)
{
  return std::to_string(inInfo.fROMVersion >> 16) + "." + std::to_string(inInfo.fROMVersion & 0xFFFF);
}


bool Is1xData(const NewtonInfo & inInfo)
{
  return inInfo.fRAMSize == 0;
}

} // namespace ncx
