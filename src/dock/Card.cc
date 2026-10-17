/*
 File: Card.cc

 Reading a Newton's PCMCIA cards (Card.h).
 */

#include "dock/Card.h"
#include "dock/MemoryDump.h"
#include "base/Log.h"

#include "CIS.h"          // newton-framework's Matt/NewtDump

#include <fstream>

namespace ncx {

int SocketCount(const NewtonInfo & inInfo)
{
  switch (inInfo.fMachineType) {
  case 0x10003000: return 2;      // MessagePad 2000/2100 (and Einstein)
  case 0x10004000: return 1;      // eMate 300
  }
  return 0;
}


std::string DescribePins(uint32_t inPins)
{
  if ((inPins & 0x000C) == 0x000C)
    return "no card";
  if (!CardInserted(inPins))
    return "a card, not all the way in";
  bool vs1 = inPins & 0x0010, vs2 = inPins & 0x0020;
  std::string text = vs1 && vs2 ? "a card, 5 V" : vs1 ? "a card, X.X V (not supported)" : "a card, 3.3 V";
  if (inPins & 0x0200)
    text += ", write protected";
  return text;
}


int CardReader::sockets(const NewtonInfo & inInfo, std::vector<CardSocket> & outSockets)
{
  outSockets.clear();
  for (int s = 0; s < SocketCount(inInfo); ++s) {
    CardSocket socket;
    socket.socket = s;
    int error = mReader.readRegister(SocketBase(s) + kCardControlOffset + kCardRegPins, socket.pins);
    if (error != 0) {
      mWhy = "reading the pins of socket " + std::to_string(s) + " failed";
      return error;
    }
    socket.inserted = CardInserted(socket.pins);
    outSockets.push_back(socket);
  }
  return 0;
}


int CardReader::dump(int inSocket, const std::filesystem::path & inFolder, const std::string & inHeader, uint64_t inSize,
                     bool inResume, uint32_t inBlock, Progress * ioProgress, CardDump & outDump)
{
  mWhy.clear();
  outDump = CardDump();
  outDump.socket = inSocket;
  uint32_t pins = 0;
  int error = mReader.readRegister(SocketBase(inSocket) + kCardControlOffset + kCardRegPins, pins);
  if (error != 0) {
    mWhy = "reading the pins of socket " + std::to_string(inSocket) + " failed";
    return error;
  }
  if (!CardInserted(pins)) {
    mWhy = "socket " + std::to_string(inSocket) + ": " + DescribePins(pins);
    return kDockErrCardNotInstalled;
  }
  // powered while it's read, let go afterwards (also after an error),
  // unless the Newton ended it
  int32_t on = 0;
  error = mReader.power(inSocket, true, &on);
  if (error != 0) {
    mWhy = "the card in socket " + std::to_string(inSocket) + " can't be powered";
    return error;
  }
  Log(kLogMinimum, "card: socket %d powered (VccOn %d)", inSocket, (int)on);
  error = read(inSocket, inFolder, inHeader, inSize, inResume, inBlock, ioProgress, outDump);
  if (error != kDockErrAborted || (ioProgress && ioProgress->cancelled()))
    mReader.power(inSocket, false);
  return error;
}


int CardReader::read(int inSocket, const std::filesystem::path & inFolder, const std::string & inHeader, uint64_t inSize,
                     bool inResume, uint32_t inBlock, Progress * ioProgress, CardDump & outDump)
{
  uint32_t base = SocketBase(inSocket);
  std::string prefix = "card" + std::to_string(inSocket);
  outDump.cisFile = inFolder / (prefix + "-cis.txt");
  outDump.attributeFile = inFolder / (prefix + "-attribute.bin");
  outDump.commonFile = inFolder / (prefix + "-common.bin");

  // the CIS: attribute memory, a byte at a time
  if (ioProgress)
    ioProgress->setDescription("Socket " + std::to_string(inSocket) + ": the CIS");
  mReader.setLabel("Reading card " + std::to_string(inSocket) + "'s CIS");
  int error = DumpMemory(mReader, base + kCardAttributeOffset, kCardAttributeRead, outDump.attributeFile, false, inBlock,
                         nullptr, true);
  if (error != 0) {
    mWhy = "reading the attribute memory failed";
    return error;
  }
  std::vector<uint8_t> attribute(kCardAttributeRead);
  {
    std::ifstream in(outDump.attributeFile, std::ios::binary);
    in.read((char *)attribute.data(), (std::streamsize)attribute.size());
    attribute.resize((size_t)in.gcount());
  }
  CISInfo cis = ParseCIS(attribute);
  outDump.cisWhere = "attribute memory";
  if (!cis.valid) {
    // a ROM card may keep it at the start of common memory
    std::vector<uint8_t> common;
    error = mReader.read(base + kCardCommonOffset, 4096, common);
    if (error != 0) {
      mWhy = "reading the start of common memory failed";
      return error;
    }
    cis = ParseCIS(common, true);
    outDump.cisWhere = "common memory";
  }
  outDump.cisFound = cis.valid;
  if (cis.valid) {
    outDump.cis.push_back("CIS of the card in socket " + std::to_string(inSocket) + " (in " + outDump.cisWhere + ", "
                          + cis.layout + "):");
    outDump.cis.insert(outDump.cis.end(), cis.report.begin(), cis.report.end());
  } else {
    outDump.cis.push_back("No CIS found on the card in socket " + std::to_string(inSocket)
                          + " (attribute memory, start of common memory).");
  }
  {
    std::ofstream out(outDump.cisFile, std::ios::binary);
    out << inHeader << "\n";
    for (const std::string & line : outDump.cis)
      out << line << "\n";
  }

  // the common memory, as big as the CIS says
  uint64_t size = inSize ? inSize : cis.commonSize;
  if (size == 0) {
    mWhy = "the card's size is unknown (its CIS has no device entry)";
    return kDockErrDesktopError;
  }
  if (size > kCardMaxSize) {
    mWhy = "the card says it has " + std::to_string(size >> 20) + " MB, more than a socket's 64 MB";
    return kDockErrDesktopError;
  }
  if (ioProgress)
    ioProgress->setDescription("Socket " + std::to_string(inSocket) + ": the memory");
  mReader.setLabel("Reading card " + std::to_string(inSocket));
  error = DumpMemory(mReader, base + kCardCommonOffset, size, outDump.commonFile, inResume, inBlock, ioProgress);
  if (error != 0) {
    mWhy = error == kDockErrAborted ? "cancelled" : "reading the common memory failed";
    return error;
  }
  outDump.commonSize = size;
  return 0;
}

} // namespace ncx
