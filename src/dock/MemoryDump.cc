/*
 File: MemoryDump.cc

 Reading a Newton's physical memory (MemoryDump.h).
 */

#include "dock/MemoryDump.h"
#include "dock/DockController.h"
#include "base/Log.h"
#include "comms/DockRefs.h"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace ncx {

namespace {

const EventType kMemoryExtension = Cmd("NDmp");
const EventType kMemoryAnswer = Cmd("NDrs");

Ref Request(const char * inOp)
{
  RefVar request(AllocateFrame());
  SetFrameSlot(request, MakeSymbol("op"), MakeSymbol(inOp));
  return request;
}

} // namespace


int MemoryReader::begin()
{
  mDock.setDesktopControl(true);
  mDock.loadExtension(kMemoryExtension);
  RefVar answer;
  int error = ask(RefVar(Request("hello")), answer);
  if (error != 0)
    mDock.setDesktopControl(false);
  return error;
}


void MemoryReader::end()
{
  mDock.setDesktopControl(false);
}


int MemoryReader::ask(RefArg inRequest, RefVar & outAnswer)
{
  outAnswer = NILREF;
  Session & session = mDock.session();
  session.sendRef(kMemoryExtension, inRequest);
  for (;;) {
    DockEvent answer = session.receive(kDAnyEvent);
    if (answer.command == kMemoryAnswer) {
      // an integer comes as a 4-byte long, not NSOF (TDocker::WriteCommand)
      outAnswer = answer.data.size() == 4 ? MAKEINT(answer.value()) : EventRef(answer);
      break;
    }
    if (answer.command == kDOperationCanceled) {
      // Disconnect on the Newton: it waits for the acknowledgement
      session.send(kDOpCanceledAck);
      return kDockErrAborted;
    }
    if (answer.command == kDDisconnect)
      throw DockFailure(kDockErrDisconnected, "the Newton ended the connection");
    if (answer.command == kDResult)
      return answer.value() != 0 ? answer.value() : kDockErrProtocolError;
    Log(kLogMinimum, "memory: '%s' instead of an answer, ignored", CmdName(answer.command).c_str());
  }
  RefVar error(IsFrame(outAnswer) ? GetFrameSlot(outAnswer, MakeSymbol("error")) : (Ref)NILREF);
  if (ISINT(error)) {
    Log(kLogMinimum, "memory: the Newton's error %d", (int)RINT(error));
    return RINT(error) != 0 ? (int)RINT(error) : -1;
  }
  return 0;
}


int MemoryReader::map(uint32_t inBase, uint32_t inSize, int & outMap)
{
  uint64_t key = (uint64_t)inBase << 32 | inSize;
  auto found = mMaps.find(key);
  if (found != mMaps.end()) {
    outMap = found->second;
    return 0;
  }
  RefVar request(Request("map"));
  SetFrameSlot(request, MakeSymbol("page"), MAKEINT((int)(inBase / kPage)));
  SetFrameSlot(request, MakeSymbol("pages"), MAKEINT((int)(inSize / kPage)));
  RefVar answer;
  int error = ask(request, answer);
  if (error != 0)
    return error;
  RefVar index(IsFrame(answer) ? GetFrameSlot(answer, MakeSymbol("map")) : (Ref)NILREF);
  if (!ISINT(index))
    return kDockErrProtocolError;
  outMap = (int)RINT(index);
  mMaps[key] = outMap;
  return 0;
}


int MemoryReader::read(uint32_t inAddress, uint32_t inLength, std::vector<uint8_t> & outBytes, bool * outUniform)
{
  uint32_t window = inAddress & ~(kWindow - 1);
  int index = 0;
  int error = map(window, kWindow, index);
  if (error != 0)
    return error;
  RefVar request(Request("read"));
  SetFrameSlot(request, MakeSymbol("map"), MAKEINT(index));
  SetFrameSlot(request, MakeSymbol("offset"), MAKEINT((int)(inAddress - window)));
  SetFrameSlot(request, MakeSymbol("length"), MAKEINT((int)inLength));
  RefVar answer;
  error = ask(request, answer);
  if (error != 0)
    return error;
  if (ISINT(answer)) {
    // all of them the same
    outBytes.assign(inLength, (uint8_t)RINT(answer));
    if (outUniform)
      *outUniform = true;
    return 0;
  }
  if (!IsBinary(answer) || Length(answer) != inLength)
    return kDockErrProtocolError;
  const uint8_t * bytes = (const uint8_t *)BinaryData(answer);
  outBytes.assign(bytes, bytes + inLength);
  if (outUniform)
    *outUniform = false;
  return 0;
}


uint32_t ROMSize(const NewtonInfo &)
{
  // every NewtonOS 2 machine NCX knows (MessagePad 2x00, eMate 300,
  // Einstein): 8 MB from physical 0
  return 8 << 20;
}


int DumpMemory(DockController & ioDock, uint32_t inAddress, uint64_t inSize, const std::filesystem::path & inFile,
               bool inResume, uint32_t inBlock, Progress * ioProgress)
{
  uint64_t start = 0;
  std::error_code ec;
  if (inResume && std::filesystem::exists(inFile, ec))
    start = std::min<uint64_t>(std::filesystem::file_size(inFile, ec), inSize) / inBlock * inBlock;
  std::fstream file(inFile, start ? std::ios::in | std::ios::out | std::ios::binary
                                  : std::ios::out | std::ios::trunc | std::ios::binary);
  if (!file)
    return kDockErrDesktopError;
  file.seekp((std::streamoff)start);
  if (ioProgress) {
    ioProgress->setTotal((int64_t)inSize);
    ioProgress->setCompleted((int64_t)start);
  }

  MemoryReader reader(ioDock);
  int error = reader.begin();
  uint64_t done = start;
  std::vector<uint8_t> bytes;
  while (error == 0 && done < inSize) {
    if (ioProgress && ioProgress->cancelled()) {
      error = kDockErrAborted;
      break;
    }
    uint32_t address = inAddress + (uint32_t)done;
    uint32_t length = (uint32_t)std::min<uint64_t>(inBlock, inSize - done);
    uint32_t toWindowEnd = MemoryReader::kWindow - (address & (MemoryReader::kWindow - 1));
    length = std::min(length, toWindowEnd);
    error = reader.read(address, length, bytes);
    if (error != 0)
      break;
    file.write((const char *)bytes.data(), (std::streamsize)bytes.size());
    if (!file) {
      error = kDockErrDesktopError;
      break;
    }
    done += length;
    if (ioProgress)
      ioProgress->setCompleted((int64_t)done);
  }
  file.close();
  // cancelled on the Newton: it no longer waits for an end of control
  if (error != kDockErrAborted)
    reader.end();
  return error;
}

} // namespace ncx
