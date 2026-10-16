/*
 File: FakeNewton.cc

 A Newton for NCX's tests (FakeNewton.h).
 */

#include "FakeNewton.h"

#include "base/Core.h"
#include "base/NewtonStrings.h"
#include "base/Strings.h"
#include "comms/DockRefs.h"
#include "comms/Handshake.h"

#include "Matt/MNP.h"
#include "Matt/NTKTransport.h"
#include "RSSymbols.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <memory>
#include <thread>

namespace ncx {

namespace {

using Clock = std::chrono::steady_clock;

void PutLong(std::vector<uint8_t> & ioBytes, uint32_t inValue)
{
  for (int shift = 24; shift >= 0; shift -= 8)
    ioBytes.push_back((uint8_t)(inValue >> shift));
}


/*-- the wire: MNP as the Newton, or plain TCP --*/

class Wire
{
public:
  virtual ~Wire() { }
  virtual bool connect(int inPort, double inSeconds, std::string & outWhy) = 0;
  virtual bool send(const DockEvent & inEvent) = 0;
  /** The next event; false after inSeconds or when the wire is gone. */
  bool receive(DockEvent & outEvent, double inSeconds)
  {
    Clock::time_point end = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(inSeconds));
    while (mEvents.empty()) {
      if (Clock::now() > end || mGone)
        return false;
      std::vector<uint8_t> data;
      if (!pump(data, 100))
        mGone = true;
      if (!data.empty())
        feed(data);
    }
    outEvent = std::move(mEvents.front());
    mEvents.erase(mEvents.begin());
    return true;
  }
protected:
  /** Bytes that came within inMilliseconds; false when gone. */
  virtual bool pump(std::vector<uint8_t> & outData, int inMilliseconds) = 0;
  void feed(const std::vector<uint8_t> & inData)
  {
    std::vector<DockEvent> events;
    mParser.feed(inData.data(), inData.size(), events);
    // an event of no set length (cgfn, crmf) ends where its two objects end
    EventType command;
    const std::vector<uint8_t> * data;
    if (mParser.openEnded(command, data)) {
      size_t end1 = 0, end2 = 0;
      UnflattenBytes(*data, 0, &end1);
      if (end1 > 0)
        UnflattenBytes(*data, end1, &end2);
      if (end2 > end1)
        mParser.endOpenEnded(end2, events);
    }
    for (DockEvent & event : events)
      mEvents.push_back(std::move(event));
  }
private:
  DockEventParser mParser;
  std::vector<DockEvent> mEvents;
  bool mGone = false;
};


class MNPWire : public Wire
{
public:
  ~MNPWire() override { mLink.stop(); }

  bool connect(int inPort, double inSeconds, std::string & outWhy) override
  {
    mLink.setRole(MNPLink::kNewton);
    Clock::time_point end = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(inSeconds));
    while (!mLink.start("tcp-client:" + std::to_string(inPort), outWhy)) {
      if (Clock::now() > end)
        return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // the link: our LR, the desktop's LR, our LA
    while (!mLink.isUp()) {
      std::vector<uint8_t> ignored;
      if (Clock::now() > end || !pump(ignored, 100)) {
        outWhy = "the MNP link didn't come up";
        return false;
      }
    }
    return true;
  }

  bool send(const DockEvent & inEvent) override
  {
    mLink.send(inEvent.encode());
    return true;
  }

protected:
  bool pump(std::vector<uint8_t> & outData, int inMilliseconds) override
  {
    int fd = mLink.notifyFd();
    bool ready = false;
    NTKWaitReadable(&fd, 1, inMilliseconds, &ready);
    MNPLink::Event event;
    while (mLink.nextEvent(event)) {
      if (event.kind == MNPLink::Event::kData)
        outData.insert(outData.end(), event.data.begin(), event.data.end());
      else if (event.kind == MNPLink::Event::kLinkDown)
        mDown = true;
    }
    return !mDown;
  }

private:
  MNPLink mLink;
  bool mDown = false;
};


class PlainWire : public Wire
{
public:
  bool connect(int inPort, double inSeconds, std::string & outWhy) override
  {
    Clock::time_point end = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(inSeconds));
    for (;;) {
      mTransport.reset(NTKTransport::Create("tcp-client:" + std::to_string(inPort), outWhy));
      if (mTransport && mTransport->start(outWhy))
        return true;
      if (Clock::now() > end)
        return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  bool send(const DockEvent & inEvent) override
  {
    std::vector<uint8_t> bytes = inEvent.encode();
    return mTransport->send(bytes.data(), bytes.size());
  }

protected:
  bool pump(std::vector<uint8_t> & outData, int inMilliseconds) override
  {
    int fd = mTransport->fd();
    bool ready = false;
    if (fd < 0)
      return false;
    NTKWaitReadable(&fd, 1, inMilliseconds, &ready);
    if (!ready)
      return true;
    return mTransport->handleReadable(outData) != NTKTransport::kDisconnected;
  }

private:
  std::unique_ptr<NTKTransport> mTransport;
};


/*-- the fixture --*/

Ref MakeEntry(int inId, const char * inFirst, const char * inLast)
{
  RefVar name(AllocateFrame());
  SetFrameSlot(name, MakeSymbol("first"), MakeStringFromUTF8(inFirst));
  SetFrameSlot(name, MakeSymbol("last"), MakeStringFromUTF8(inLast));
  RefVar entry(AllocateFrame());
  SetFrameSlot(entry, SYMA(_uniqueId), MAKEINT(inId));
  SetFrameSlot(entry, MakeSymbol("cardType"), MAKEINT(0));
  SetFrameSlot(entry, SYMA(name), name);
  return entry;
}

Ref MakeStores()
{
  RefVar store(AllocateFrame());
  SetFrameSlot(store, SYMA(name), MakeStringFromUTF8("Internal"));
  SetFrameSlot(store, SYMA(kind), MakeStringFromUTF8("Internal"));
  SetFrameSlot(store, SYMA(signature), MAKEINT(1234567));
  SetFrameSlot(store, MakeSymbol("totalSize"), MAKEINT(1048576));
  SetFrameSlot(store, MakeSymbol("usedSize"), MAKEINT(262144));
  RefVar stores(MakeArray(1));
  SetArraySlot(stores, 0, store);
  return stores;
}

} // namespace


bool FakeNewton::run(const FakeNewtonOptions & inOptions)
{
  InitCore();
  mReceived.clear();
  std::unique_ptr<Wire> wire;
  if (inOptions.plainTCP)
    wire.reset(new PlainWire());
  else
    wire.reset(new MNPWire());
  if (!wire->connect(inOptions.port, inOptions.connectSeconds, mWhy))
    return false;

  DockEvent event;
  auto expect = [&](EventType inCommand) -> bool {
    if (!wire->receive(event, 30)) {
      mWhy = "nothing from the desktop (waiting for '" + CmdName(inCommand) + "')";
      return false;
    }
    mReceived.push_back(CmdName(event.command));
    if (event.command != inCommand) {
      mWhy = "'" + CmdName(event.command) + "' instead of '" + CmdName(inCommand) + "'";
      return false;
    }
    return true;
  };

  // the handshake, the Newton's side
  wire->send(DockEvent::MakeValue(kDRequestToDock, kBaseProtocolVersion));
  if (!expect(kDInitiateDocking))
    return false;
  std::vector<uint8_t> name;
  const uint32_t info[] = { 0x01020304, 0x01000000, 0x10003000, 0x00020002, 0x00008000, 4 << 20,
                            480, 320, 0, 0x00020002, 1234567, 100, 100, 4, 0, 0, 0, 0 };
  PutLong(name, sizeof(info));
  for (uint32_t v : info)
    PutLong(name, v);
  std::u16string owner = UTF16FromUTF8(inOptions.name);
  for (char16_t c : owner) {
    name.push_back((uint8_t)(c >> 8));
    name.push_back((uint8_t)c);
  }
  name.push_back(0);
  name.push_back(0);
  wire->send(DockEvent::MakeData(kDNewtonName, std::move(name)));

  if (!expect(kDDesktopInfo))
    return false;
  uint32_t deskHi = event.longAt(8), deskLo = event.longAt(12);
  RefVar apps(UnflattenBytes(event.data, 24));
  if (!IsArray(apps) || Length(apps) != 1) {
    mWhy = "the desktop's info has no app";
    return false;
  }
  uint32_t newtHi = 0x11223344, newtLo = 0x55667788;
  std::vector<uint8_t> newtonInfo;
  PutLong(newtonInfo, kDanteProtocolVersion);
  PutLong(newtonInfo, newtHi);
  PutLong(newtonInfo, newtLo);
  wire->send(DockEvent::MakeData(kDNewtonInfo, std::move(newtonInfo)));
  if (!expect(kDWhichIcons))
    return false;
  wire->send(DockEvent::MakeValue(kDResult, 0));
  if (!expect(kDSetTimeout))
    return false;

  // the password: the desktop's nonce, encrypted with ours
  EncryptNonce(deskHi, deskLo, inOptions.password);
  for (;;) {
    std::vector<uint8_t> password;
    PutLong(password, deskHi);
    PutLong(password, deskLo);
    wire->send(DockEvent::MakeData(kDPassword, std::move(password)));
    if (!wire->receive(event, 30)) {
      mWhy = "no answer to the password";
      return false;
    }
    mReceived.push_back(CmdName(event.command));
    if (event.command == kDPassword)
      break;
    if (event.command != kDPWWrong) {
      mWhy = "the desktop refused the password ('" + CmdName(event.command) + "')";
      return false;
    }
    // a wrong password: try again with the same (a test of the retries)
  }
  EncryptNonce(newtHi, newtLo, inOptions.password);
  if (event.longAt(0) != newtHi || event.longAt(4) != newtLo) {
    mWhy = "the desktop's answer to our nonce is wrong";
    return false;
  }

  // the session: answer until 'disc'
  auto hasExtension = [this](const char * inId) {
    for (const std::string & id : mExtensions)
      if (id == inId)
        return true;
    return false;
  };
  RefVar entries(MakeArray(2));
  SetArraySlot(entries, 0, MakeEntry(100, "Ada", "Lovelace"));
  SetArraySlot(entries, 1, MakeEntry(101, "Charles", "Babbage"));
  int cursorPosition = 0;
  for (;;) {
    if (!wire->receive(event, 60)) {
      mWhy = "the desktop went away without 'disc'";
      return false;
    }
    mReceived.push_back(CmdName(event.command));
    EventType c = event.command;
    if (c == kDDisconnect)
      return true;
    if (c == kDDesktopInControl || c == kDOperationDone || c == kDHello)
      continue;
    if (c == kDGetStoreNames) {
      wire->send(MakeRefEvent(kDStoreNames, MakeStores()));
    } else if (c == kDGetDefaultStore) {
      wire->send(MakeRefEvent(kDDefaultStore, GetArraySlot(MakeStores(), 0)));
    } else if (c == kDSetCurrentStore || c == kDSetStoreToDefault || c == kDSetCurrentSoup || c == kDCursorFree) {
      wire->send(DockEvent::MakeValue(kDResult, 0));
    } else if (c == kDGetSoupNames) {
      RefVar names(MakeArray(2)), signatures(MakeArray(2));
      SetArraySlot(names, 0, MakeStringFromUTF8("Names"));
      SetArraySlot(names, 1, MakeStringFromUTF8("Notes"));
      SetArraySlot(signatures, 0, MAKEINT(11));
      SetArraySlot(signatures, 1, MAKEINT(12));
      std::vector<uint8_t> data = FlattenToBytes(names), more = FlattenToBytes(signatures);
      data.insert(data.end(), more.begin(), more.end());
      wire->send(DockEvent::MakeData(kDSoupNames, std::move(data)));
    } else if (c == kDGetSoupIDs) {
      std::vector<uint8_t> ids;
      PutLong(ids, 2);
      PutLong(ids, 100);
      PutLong(ids, 101);
      wire->send(DockEvent::MakeData(kDSoupIDs, std::move(ids)));
    } else if (c == kDReturnEntry) {
      int id = event.value();
      wire->send(MakeRefEvent(kDEntry, id == 101 ? GetArraySlot(entries, 1) : GetArraySlot(entries, 0)));
    } else if (c == kDQuery) {
      cursorPosition = 0;
      wire->send(DockEvent::MakeValue(kDLongData, 7));          // the cursor's id
    } else if (c == kDCursorCountEntries) {
      wire->send(DockEvent::MakeValue(kDLongData, 2));
    } else if (c == kDCursorEntry || c == kDCursorNext || c == kDCursorReset) {
      if (c == kDCursorNext)
        ++cursorPosition;
      if (c == kDCursorReset)
        cursorPosition = 0;
      RefVar entry(cursorPosition < 2 ? GetArraySlot(entries, cursorPosition) : (Ref)NILREF);
      wire->send(MakeRefEvent(kDRefResult, entry));
    } else if (c == kDRegProtocolExtension) {
      if (event.data.size() > 4)
        mExtensions.push_back(CmdName(event.longAt(0)));
      wire->send(DockEvent::MakeValue(kDResult, 0));
    } else if (c == Cmd("scrn") && hasExtension("scrn")) {
      wire->send(DockEvent::MakeValue(kDResult, inOptions.toolkit ? 0 : kDockErrFileNotFound));
    } else if (c == Cmd("NCss") && hasExtension("NCss")) {
      RefVar request(UnflattenBytes(event.data));
      RefVar op(IsFrame(request) ? GetFrameSlot(request, MakeSymbol("op")) : (Ref)NILREF);
      // symbols compare without case, as on a Newton ('read may come as 'Read)
      std::string what = IsSymbol(op) ? UTF8FromSymbol(op) : std::string();
      for (char & ch : what)
        ch = (char)std::tolower((unsigned char)ch);
      RefVar answer(AllocateFrame());
      if (!inOptions.toolkit) {
        SetFrameSlot(answer, MakeSymbol("error"), MAKEINT(kDockErrFileNotFound));
      } else if (what == "take") {
        SetFrameSlot(answer, MakeSymbol("top"), MAKEINT(0));
        SetFrameSlot(answer, MakeSymbol("left"), MAKEINT(0));
        SetFrameSlot(answer, MakeSymbol("bottom"), MAKEINT(480));
        SetFrameSlot(answer, MakeSymbol("right"), MAKEINT(320));
        SetFrameSlot(answer, MakeSymbol("rowBytes"), MAKEINT(160));
        SetFrameSlot(answer, MakeSymbol("depth"), MAKEINT(4));
        SetFrameSlot(answer, MakeSymbol("size"), MAKEINT(160 * 480));
        if (NOTNIL(GetFrameSlot(request, MakeSymbol("bits")))) {
          RefVar all(AllocateBinary(MakeSymbol("bits"), 160 * 480));
          uint8_t * bits = (uint8_t *)BinaryData(all);
          for (int at = 0; at < 160 * 480; ++at)
            bits[at] = (uint8_t)(ScreenPixel((at % 160) * 2, at / 160) << 4 | ScreenPixel((at % 160) * 2 + 1, at / 160));
          SetFrameSlot(answer, MakeSymbol("theBits"), all);
        }
      } else if (what == "read") {
        int offset = (int)RINT(GetFrameSlot(request, MakeSymbol("offset")));
        int length = (int)RINT(GetFrameSlot(request, MakeSymbol("length")));
        length = std::max(0, std::min(length, 160 * 480 - offset));
        answer = AllocateBinary(MakeSymbol("bits"), length);
        uint8_t * bits = (uint8_t *)BinaryData(answer);
        for (int i = 0; i < length; ++i) {
          int at = offset + i, y = at / 160, x = (at % 160) * 2;
          bits[i] = (uint8_t)(ScreenPixel(x, y) << 4 | ScreenPixel(x + 1, y));
        }
        mLargestBlock = std::max(mLargestBlock, (size_t)length);
      } else {
        SetFrameSlot(answer, MakeSymbol("done"), TRUEREF);
      }
      wire->send(MakeRefEvent(Cmd("NCsr"), answer));
    } else if (c == kDLoadPackage) {
      mPackageSize = event.data.size();
      wire->send(DockEvent::MakeValue(kDResult, 0));
    } else if (c == kDCallGlobalFunction) {
      size_t end = 0;
      RefVar function(UnflattenBytes(event.data, 0, &end));
      RefVar args(UnflattenBytes(event.data, end));
      RefVar result;
      if (IsSymbol(function) && UTF8FromSymbol(function) == "GetUserConfig")
        result = MAKEINT(12290);
      else
        result = AllocateFrame();
      wire->send(MakeRefEvent(kDCallResult, result));
    } else {
      wire->send(DockEvent::MakeValue(kDResult, kDockErrProtocolError));
    }
  }
}

} // namespace ncx
