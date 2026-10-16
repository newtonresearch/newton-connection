/*
 File: MemoryDump.h

 Reading a Newton's physical memory (PORTING.md 5.2.2): its ROM, which the
 Cocoa app read a 4 KB page at a time through virtual memory ('reqp',
 NCROMDumpComponent). NCX now does it as newtdump does, with its extension
 'NDmp' (newton-framework's Matt/NewtDump): physical memory mapped read only
 and uncached, a 1 MB window at a time (a window stays mapped until the
 Newton restarts, shared with newtdump), read 2 KB at a time; a block whose
 bytes are all the same comes as that byte. It works on any NewtonOS 2
 ROM build (the extension's natives use the ROM's jump table): MessagePad
 2x00, eMate 300, Einstein. On the dock thread.
 */

#ifndef NCX_DOCK_MEMORYDUMP_H
#define NCX_DOCK_MEMORYDUMP_H

#include "base/Progress.h"
#include "comms/DockProtocol.h"

#include "Objects.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <vector>

namespace ncx {

class DockController;

class MemoryReader
{
public:
  static constexpr uint32_t kBlock = 2048;          // bytes a request (as newtdump)
  static constexpr uint32_t kWindow = 1 << 20;      // mapped at a time
  static constexpr uint32_t kPage = 4096;

  explicit MemoryReader(DockController & inDock) : mDock(inDock) { }

  /** Take control, send the extension. 0 or the Newton's error. */
  int begin();
  /** inLength bytes at the physical inAddress (a multiple of 4, not across
      a 1 MB window): 0 and outBytes (outUniform: all the same), or an
      error (kDockErrAborted: the user cancelled on the Newton). */
  int read(uint32_t inAddress, uint32_t inLength, std::vector<uint8_t> & outBytes, bool * outUniform = nullptr);
  /** Give up control. */
  void end();

private:
  int ask(RefArg inRequest, RefVar & outAnswer);
  int map(uint32_t inBase, uint32_t inSize, int & outMap);

  DockController & mDock;
  std::map<uint64_t, int> mMaps;          // (base, size) -> the Newton's map
};

/** The size of a Newton's ROM: 8 MB (MessagePad 2x00, eMate 300, Einstein). */
uint32_t ROMSize(const NewtonInfo & inInfo);

/** inSize bytes of physical memory from inAddress into inFile, a block at a
    time (inBlock: 256 ... 16384, a power of 2). inResume: after the whole
    blocks the file has already. 0 when all of it is there; otherwise the
    error, and the file keeps what came (to resume). ioProgress: the bytes
    there of inSize; cancel() stops it (kDockErrAborted). */
int DumpMemory(DockController & ioDock, uint32_t inAddress, uint64_t inSize, const std::filesystem::path & inFile,
               bool inResume, uint32_t inBlock = MemoryReader::kBlock, Progress * ioProgress = nullptr);

} // namespace ncx

#endif // NCX_DOCK_MEMORYDUMP_H
