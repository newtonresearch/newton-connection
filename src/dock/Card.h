/*
 File: Card.h

 Reading a Newton's PCMCIA cards (PORTING.md 5.2.9), as newtdump does it
 (newton-framework's Matt/NewtDump), through the same extension as the ROM
 dump (MemoryDump.h):
   - Physical addresses (MessagePad 2x00, eMate 300, Einstein): socket n
     at 0x30000000 + n * 0x10000000; in it attribute memory at +0, common
     memory at +0x08000000, the control registers at +0x0C000000 (+0x1C00
     the pins: both card-detect pins clear is a card).
   - A socket is touched only if its pins say a card is in it. The card is
     powered (VccOn) while it is read, and let go afterwards (VccOff),
     also when reading fails, unless the Newton ended the session.
   - The CIS: the first 8 KB of attribute memory, read a byte at a time
     (as the ROM does), decoded by newtdump's CIS.cc; a ROM card may keep
     it at the start of common memory instead. It says how big the common
     memory is.
   - Into a folder, as newtdump names them: card<n>-cis.txt (decoded),
     card<n>-attribute.bin, card<n>-common.bin.

 On the dock thread.
 */

#ifndef NCX_DOCK_CARD_H
#define NCX_DOCK_CARD_H

#include "base/Progress.h"
#include "comms/DockProtocol.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ncx {

class MemoryReader;

inline constexpr uint32_t kSocketBase0 = 0x30000000;
inline constexpr uint32_t kSocketStride = 0x10000000;
inline constexpr uint32_t kCardAttributeOffset = 0;
inline constexpr uint32_t kCardCommonOffset = 0x08000000;
inline constexpr uint32_t kCardControlOffset = 0x0C000000;
inline constexpr uint32_t kCardRegPins = 0x1C00;
inline constexpr uint32_t kCardRegVcc = 0x2800;
inline constexpr uint32_t kCardRegVoltage = 0x2C00;
inline constexpr uint32_t kCardAttributeRead = 8192;     // 4 KB of CIS bytes
inline constexpr uint64_t kCardMaxSize = 64 << 20;       // a socket's common memory

/** The PCMCIA sockets of a Newton: 2 (MessagePad 2x00, Einstein), 1 (eMate
    300), 0 for one NCX doesn't know. */
int SocketCount(const NewtonInfo & inInfo);
inline uint32_t SocketBase(int inSocket) { return kSocketBase0 + (uint32_t)inSocket * kSocketStride; }

/** A socket's pins: a card in it (both card-detect pins clear). */
inline bool CardInserted(uint32_t inPins) { return (inPins & 0x000C) == 0; }
/** The pins for people: "a card, 5 V, write protected", "no card", ... */
std::string DescribePins(uint32_t inPins);

struct CardSocket
{
  int socket = 0;
  uint32_t pins = 0;
  bool inserted = false;
};

struct CardDump
{
  int socket = 0;
  bool cisFound = false;
  std::string cisWhere;                   // "attribute memory" or "common memory"
  std::vector<std::string> cis;           // the decoded CIS, a line each
  uint64_t commonSize = 0;                // what was read of common memory
  std::filesystem::path cisFile, attributeFile, commonFile;
};

class CardReader
{
public:
  /** inReader has begun (MemoryReader::begin). */
  explicit CardReader(MemoryReader & ioReader) : mReader(ioReader) { }

  /** The sockets of this Newton, with their pins. 0 or the error. */
  int sockets(const NewtonInfo & inInfo, std::vector<CardSocket> & outSockets);

  /** The card in inSocket into inFolder: its CIS, its attribute memory and
      its common memory (inSize bytes, or as big as the CIS says). inHeader:
      the first line of the CIS file (the Newton). inResume: after what the
      common memory file has. ioProgress: the bytes of common memory there;
      cancel() stops it (kDockErrAborted). 0, or the error (and why()). */
  int dump(int inSocket, const std::filesystem::path & inFolder, const std::string & inHeader, uint64_t inSize,
           bool inResume, uint32_t inBlock, Progress * ioProgress, CardDump & outDump);

  const std::string & why() const { return mWhy; }

private:
  int read(int inSocket, const std::filesystem::path & inFolder, const std::string & inHeader, uint64_t inSize,
           bool inResume, uint32_t inBlock, Progress * ioProgress, CardDump & outDump);

  MemoryReader & mReader;
  std::string mWhy;
};

} // namespace ncx

#endif // NCX_DOCK_CARD_H
