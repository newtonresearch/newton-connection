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
#include <cstring>
#include <memory>
#include <set>
#include <thread>

namespace ncx {

namespace {

using Clock = std::chrono::steady_clock;

void PutLong(std::vector<uint8_t> & ioBytes, uint32_t inValue)
{
  for (int shift = 24; shift >= 0; shift -= 8)
    ioBytes.push_back((uint8_t)(inValue >> shift));
}


} // namespace


/*-- the wire: MNP as the Newton, or plain TCP (FakeNewton.h names it) --*/

class Wire
{
public:
  virtual ~Wire() { }
  virtual bool connect(int inPort, double inSeconds, std::string & outWhy) = 0;
  virtual bool send(const DockEvent & inEvent) = 0;
  /** Until what was sent is through (before closing, as a Newton waits). */
  virtual void flush() { }
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

  void flush() override
  {
    // the desktop's acknowledgements (MNP sends from a thread of its own)
    for (int i = 0; i < 300 && mLink.isUp() && mLink.pending() > 0; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

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


namespace {

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


bool FakeNewton::browseAndInstall(Wire & ioWire)
{
  DockEvent event;
  auto ask = [&](const DockEvent & inRequest, EventType inAnswer) -> bool {
    ioWire.send(inRequest);
    if (!ioWire.receive(event, 30)) {
      mWhy = "no answer to '" + CmdName(inRequest.command) + "'";
      return false;
    }
    mReceived.push_back(CmdName(event.command));
    if (event.command != inAnswer) {
      mWhy = "'" + CmdName(event.command) + "' instead of '" + CmdName(inAnswer) + "'";
      return false;
    }
    return true;
  };
  auto elementName = [](RefArg inElement) {
    RefVar name(IsFrame(inElement) ? GetFrameSlot(inElement, SYMA(name)) : (Ref)inElement);
    return IsString(name) ? UTF8FromString(name) : std::string();
  };

  if (!ask(MakeRefEvent(kDRequestToBrowse, RefVar(MakeSymbol("packages"))), kDResult))
    return false;
  if (!ask(DockEvent::Make(kDGetDefaultPath), kDPath))
    return false;
  RefVar path(UnflattenBytes(event.data));
  for (ArrayIndex i = 0, count = IsArray(path) ? Length(path) : 0; i < count; ++i)
    mBrowsed.push_back("path " + elementName(GetArraySlot(path, i)));
  if (!ask(DockEvent::Make(kDGetFilesAndFolders), kDFilesAndFolders))
    return false;
  RefVar files(UnflattenBytes(event.data));
  std::string package;
  bool sub = false;
  for (ArrayIndex i = 0, count = IsArray(files) ? Length(files) : 0; i < count; ++i) {
    RefVar file(GetArraySlot(files, i));
    std::string name = elementName(file);
    int type = (int)RINT(GetFrameSlot(file, SYMA(type)));
    mBrowsed.push_back("file " + name + " " + std::to_string(type));
    if (type == kDesktopFile && package.empty())
      package = name;
    if (type == kDesktopFolder && name == "sub")
      sub = true;
  }
  if (sub) {
    // into "sub", and back
    RefVar into(Clone(path));
    RefVar element(AllocateFrame());
    SetFrameSlot(element, SYMA(name), MakeStringFromUTF8("sub"));
    SetFrameSlot(element, SYMA(type), MAKEINT(kDesktopFolder));
    AddArraySlot(into, element);
    if (!ask(MakeRefEvent(kDSetPath, into), kDFilesAndFolders))
      return false;
    RefVar subFiles(UnflattenBytes(event.data));
    for (ArrayIndex i = 0, count = IsArray(subFiles) ? Length(subFiles) : 0; i < count; ++i)
      mBrowsed.push_back("subfile " + elementName(GetArraySlot(subFiles, i)));
    if (!ask(MakeRefEvent(kDSetPath, path), kDFilesAndFolders))
      return false;
  }
  if (package.empty()) {
    mWhy = "no package to install";
    return false;
  }
  if (!ask(MakeRefEvent(kDGetFileInfo, RefVar(MakeStringFromUTF8(package))), kDFileInfo))
    return false;
  RefVar info(UnflattenBytes(event.data));
  RefVar kind(IsFrame(info) ? GetFrameSlot(info, SYMA(kind)) : (Ref)NILREF);
  RefVar size(IsFrame(info) ? GetFrameSlot(info, SYMA(size)) : (Ref)NILREF);
  mBrowsed.push_back("info " + (IsString(kind) ? UTF8FromString(kind) : std::string("-")) + " "
                     + (ISINT(size) ? std::to_string(RINT(size)) : std::string("-")));

  // the package: the desktop sets the store, then sends it
  ioWire.send(MakeRefEvent(kDLoadPackageFile, RefVar(MakeStringFromUTF8(package))));
  for (;;) {
    if (!ioWire.receive(event, 30)) {
      mWhy = "the package didn't come";
      return false;
    }
    mReceived.push_back(CmdName(event.command));
    if (event.command == kDSetStoreToDefault || event.command == kDSetCurrentStore) {
      ioWire.send(DockEvent::MakeValue(kDResult, 0));
    } else if (event.command == kDLoadPackage) {
      ++mPackagesLoaded;
      mPackageSize = event.data.size();
      ioWire.send(DockEvent::MakeValue(kDResult, 0));
      break;
    } else {
      mWhy = "'" + CmdName(event.command) + "' instead of the package";
      return false;
    }
  }
  // done: the Newton ends the session (and waits until 'disc' is through)
  ioWire.send(DockEvent::Make(kDDisconnect));
  ioWire.flush();
  return true;
}


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
  const uint32_t info[] = { 0x01020304, 0x01000000, 0x10003000, inOptions.newton1 ? 0x00010003u : 0x00020002u,
                            0x00008000, 4 << 20, 480, 320, 0, 0x00020002, 1234567, 100, 100, 4, 0, 0, 0, 0 };
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

  if (inOptions.newton1) {
    // the old protocol: the timeout, and the session is on
    if (!expect(kDSetTimeout))
      return false;
    wire->send(DockEvent::MakeValue(kDResult, 0));
  } else {
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
  }

  if (inOptions.browseInstall)
    return browseAndInstall(*wire);

  // the session: answer until 'disc'
  const EventType kPackageFinder = Cmd("pfnd");
  // the soups to back up (round 2: Charles Babbage gone, Mary Somerville new)
  struct FakeSoup { std::string name; int signature; std::string app; std::string appClass; RefStruct entries; };
  std::vector<FakeSoup> soups;
  auto addSoup = [&](const char * inName, int inSignature, const char * inApp, const char * inClass) -> Ref {
    soups.push_back({ inName, inSignature, inApp, inClass, RefStruct(MakeArray(0)) });
    return soups.back().entries;
  };
  {
    RefVar names(addSoup("Names", 11, "Names", ""));
    AddArraySlot(names, RefVar(MakeEntry(100, "Ada", "Lovelace")));
    if (inOptions.round == 1)
      AddArraySlot(names, RefVar(MakeEntry(101, "Charles", "Babbage")));
    else
      AddArraySlot(names, RefVar(MakeEntry(102, "Mary", "Somerville")));
    RefVar notes(addSoup("Notes", 12, "Notepad", ""));
    RefVar note(AllocateFrame());
    SetFrameSlot(note, SYMA(class), MakeSymbol("paragraph"));
    SetFrameSlot(note, SYMA(title), MakeStringFromUTF8("Shopping"));
    SetFrameSlot(note, SYMA(_uniqueId), MAKEINT(200));
    AddArraySlot(notes, note);
    RefVar system(addSoup("System", 13, "System information", "systemFrame"));
    RefVar config(AllocateFrame());
    SetFrameSlot(config, MakeSymbol("tag"), MakeStringFromUTF8("Config"));
    SetFrameSlot(config, SYMA(_uniqueId), MAKEINT(1));
    AddArraySlot(system, config);
    RefVar packages(addSoup("Packages", 14, "Packages", "packageFrame"));
    RefVar package(AllocateFrame());
    SetFrameSlot(package, MakeSymbol("packageName"), MakeStringFromUTF8("Tetris"));
    RefVar pkgRef(AllocateBinary(MakeSymbol("package"), 16));
    std::memcpy(BinaryData(pkgRef), "package0 a test", 16);
    SetFrameSlot(package, MakeSymbol("pkgRef"), pkgRef);
    SetFrameSlot(package, SYMA(_uniqueId), MAKEINT(300));
    AddArraySlot(packages, package);
    RefVar icon(AllocateFrame());                     // an Extras icon: not a package
    SetFrameSlot(icon, MakeSymbol("packageName"), MakeStringFromUTF8("Tetris icon"));
    SetFrameSlot(icon, SYMA(_uniqueId), MAKEINT(301));
    AddArraySlot(packages, icon);
  }
  std::string currentSoup;
  auto findSoup = [&soups](const std::string & inName) -> const FakeSoup * {
    for (const FakeSoup & soup : soups)
      if (soup.name == inName)
        return &soup;
    return nullptr;
  };
  std::set<std::string> createdSoups;       // restore: made by the desktop
  std::vector<uint32_t> maps;               // NDmp: the windows mapped
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
  // keyboard passthrough: big-endian UniChars as text, control characters as \xNN
  auto keyboardText = [](const std::vector<uint8_t> & inData, size_t inCount) {
    std::u16string text;
    for (size_t i = 0; i < inCount && 2 * i + 1 < inData.size(); ++i) {
      char16_t ch = (char16_t)(inData[2 * i] << 8 | inData[2 * i + 1]);
      if (ch == 0)
        break;
      text.push_back(ch);
    }
    std::string out;
    for (char16_t ch : text) {
      if (ch < 0x20) {
        char hex[8];
        std::snprintf(hex, sizeof(hex), "\\x%02X", (unsigned)ch);
        out += hex;
      } else {
        out += UTF8FromUTF16(std::u16string(1, ch));
      }
    }
    return out;
  };
  bool keyboardOn = false, keyboardEnding = false;
  if (inOptions.keyboard) {
    wire->send(DockEvent::Make(kDStartKeyboardPassthrough));
    keyboardOn = true;
  }
  for (;;) {
    if (!wire->receive(event, 60)) {
      mWhy = "the desktop went away without 'disc'";
      return false;
    }
    mReceived.push_back(CmdName(event.command));
    EventType c = event.command;
    if (c == kDDisconnect)
      return true;
    if (inOptions.newton1) {
      static const EventType kNewton2Only[] = { kDDesktopInControl, kDOperationDone, kDGetSyncOptions, kDGetAppNames,
        kDBackupSoup, kDSendSoup, kDCallGlobalFunction, kDAddEntryWithUniqueID, kDSetSoupSignature, kDRequestToRestore,
        kDSourceVersion, kDRequestToSync, kDStartKeyboardPassthrough, kDRegProtocolExtension, kDGetChangedInfo,
        kDGetChangedIndex };
      for (EventType only : kNewton2Only)
        if (c == only) {
          mWhy = "'" + CmdName(c) + "' (NewtonOS 2 only) to a NewtonOS 1 Newton";
          return false;
        }
    }
    if (c == kDDesktopInControl || c == kDOperationDone || c == kDHello)
      continue;
    if (c == kDOpCanceledAck) {
      if (keyboardEnding)
        mTyped.push_back("ended");
      keyboardEnding = false;
      continue;
    }
    if (c == kDStartKeyboardPassthrough) {
      wire->send(DockEvent::Make(kDStartKeyboardPassthrough));
      keyboardOn = true;
      continue;
    }
    if (c == kDKeyboardChar || c == kDKeyboardString) {
      if (!keyboardOn) {
        mWhy = "keyboard text without passthrough";
        return false;
      }
      mTyped.push_back((c == kDKeyboardChar ? "c " : "s ") + keyboardText(event.data, c == kDKeyboardChar ? 1 : event.data.size() / 2));
      if (inOptions.keyboard && !keyboardEnding && mTyped.size() == 1) {
        // the user closes the keyboard slip
        wire->send(DockEvent::Make(kDOperationCanceled));
        keyboardOn = false;
        keyboardEnding = true;
      }
      continue;
    }
    if (c == kDGetStoreNames) {
      wire->send(MakeRefEvent(kDStoreNames, MakeStores()));
    } else if (c == kDGetDefaultStore) {
      wire->send(MakeRefEvent(kDDefaultStore, GetArraySlot(MakeStores(), 0)));
    } else if (c == kDSetCurrentStore || c == kDSetStoreToDefault || c == kDCursorFree) {
      wire->send(DockEvent::MakeValue(kDResult, 0));
    } else if (c == kDSetCurrentSoup) {
      // its name: big-endian UniChars
      std::u16string name;
      for (size_t i = 0; i + 1 < event.data.size(); i += 2) {
        char16_t ch = (char16_t)(event.data[i] << 8 | event.data[i + 1]);
        if (ch == 0)
          break;
        name.push_back(ch);
      }
      currentSoup = UTF8FromUTF16(name);
      bool known = createdSoups.count(currentSoup) > 0;
      for (const FakeSoup & soup : soups)
        known = known || soup.name == currentSoup;
      wire->send(DockEvent::MakeValue(kDResult, known ? 0 : kDockErrSoupNotFound));
    } else if (c == kDGetSoupNames) {
      RefVar names(MakeArray(0)), signatures(MakeArray(0));
      for (const FakeSoup & soup : soups) {
        AddArraySlot(names, RefVar(MakeStringFromUTF8(soup.name)));
        AddArraySlot(signatures, MAKEINT(soup.signature));
      }
      std::vector<uint8_t> data = FlattenToBytes(names), more = FlattenToBytes(signatures);
      data.insert(data.end(), more.begin(), more.end());
      wire->send(DockEvent::MakeData(kDSoupNames, std::move(data)));
    } else if (c == kDGetSoupIDs || c == kDGetChangedIDs) {
      // the current soup's ids (Names' 100 and 101 without one); changed
      // since the last sync: round 2's Names 102
      std::vector<int32_t> soupIds;
      const FakeSoup * soup = findSoup(currentSoup);
      if (!soup)
        soupIds = { 100, 101 };
      else
        for (ArrayIndex i = 0; i < Length(soup->entries); ++i) {
          int32_t id = (int32_t)RINT(GetFrameSlot(RefVar(GetArraySlot(soup->entries, i)), SYMA(_uniqueId)));
          if (c == kDGetSoupIDs || id == 102)
            soupIds.push_back(id);
        }
      std::vector<uint8_t> ids;
      PutLong(ids, (uint32_t)soupIds.size());
      for (int32_t id : soupIds)
        PutLong(ids, (uint32_t)id);
      wire->send(DockEvent::MakeData(c == kDGetSoupIDs ? kDSoupIDs : kDChangedIDs, std::move(ids)));
    } else if (c == kDReturnEntry) {
      int id = event.value();
      RefVar entry(id == 101 ? GetArraySlot(entries, 1) : GetArraySlot(entries, 0));
      if (const FakeSoup * soup = findSoup(currentSoup))
        for (ArrayIndex i = 0; i < Length(soup->entries); ++i)
          if (RINT(GetFrameSlot(RefVar(GetArraySlot(soup->entries, i)), SYMA(_uniqueId))) == id)
            entry = GetArraySlot(soup->entries, i);
      wire->send(MakeRefEvent(kDEntry, entry));
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
      } else if (what == "screen" || what == "rows" || what == "take") {
        // the screen, or rows of it: 320 x 480, 4 bits a pixel, rows of 160 bytes
        int top = 0, bottom = 480;
        if (what == "rows") {
          top = (int)RINT(GetFrameSlot(request, MakeSymbol("top")));
          bottom = (int)RINT(GetFrameSlot(request, MakeSymbol("bottom")));
        }
        SetFrameSlot(answer, MakeSymbol("left"), MAKEINT(0));
        SetFrameSlot(answer, MakeSymbol("top"), MAKEINT(top));
        SetFrameSlot(answer, MakeSymbol("right"), MAKEINT(320));
        SetFrameSlot(answer, MakeSymbol("bottom"), MAKEINT(bottom));
        SetFrameSlot(answer, MakeSymbol("rowBytes"), MAKEINT(160));
        SetFrameSlot(answer, MakeSymbol("depth"), MAKEINT(4));
        if (what != "screen") {
          int size = 160 * (bottom - top);
          RefVar bits(AllocateBinary(MakeSymbol("bits"), size));
          uint8_t * p = (uint8_t *)BinaryData(bits);
          for (int at = 0; at < size; ++at) {
            int y = top + at / 160, x = (at % 160) * 2;
            p[at] = (uint8_t)(ScreenPixel(x, y) << 4 | ScreenPixel(x + 1, y));
          }
          bool white = what == "rows" && std::all_of(p, p + size, [](uint8_t b) { return b == 0; });
          if (white) {
            SetFrameSlot(answer, MakeSymbol("blank"), TRUEREF);
            ++mBlankBlocks;
          } else {
            SetFrameSlot(answer, MakeSymbol("theBits"), bits);
          }
          if (what == "rows")
            mLargestBlock = std::max(mLargestBlock, (size_t)size);
        }
      } else {
        SetFrameSlot(answer, MakeSymbol("error"), MAKEINT(-1));
      }
      wire->send(MakeRefEvent(Cmd("NCsr"), answer));
    } else if (c == Cmd("NDmp") && hasExtension("NDmp")) {
      RefVar request(UnflattenBytes(event.data));
      RefVar op(IsFrame(request) ? GetFrameSlot(request, MakeSymbol("op")) : (Ref)NILREF);
      std::string what = IsSymbol(op) ? UTF8FromSymbol(op) : std::string();
      for (char & ch : what)
        ch = (char)std::tolower((unsigned char)ch);
      auto intSlot = [&](const char * inSlot) { return (uint32_t)RINT(GetFrameSlot(request, MakeSymbol(inSlot))); };
      RefVar answer(AllocateFrame());
      if (what == "hello") {
        SetFrameSlot(answer, MakeSymbol("version"), MAKEINT(1));
        SetFrameSlot(answer, MakeSymbol("maps"), MAKEINT((int)maps.size()));
      } else if (what == "map") {
        maps.push_back(intSlot("page") * 4096);
        SetFrameSlot(answer, MakeSymbol("map"), MAKEINT((int)maps.size() - 1));
      } else if (what == "read" && intSlot("map") < maps.size()) {
        uint32_t address = maps[intSlot("map")] + intSlot("offset"), length = intSlot("length");
        mMemoryRead += length;
        bool uniform = true;
        for (uint32_t i = 1; i < length && uniform; ++i)
          uniform = MemoryByte(address + i) == MemoryByte(address);
        if (uniform) {
          // an integer: a 4-byte long (TDocker::WriteCommand)
          wire->send(DockEvent::MakeValue(Cmd("NDrs"), MemoryByte(address)));
          continue;
        }
        answer = AllocateBinary(MakeSymbol("data"), length);
        uint8_t * p = (uint8_t *)BinaryData(answer);
        for (uint32_t i = 0; i < length; ++i)
          p[i] = MemoryByte(address + i);
      } else {
        SetFrameSlot(answer, MakeSymbol("error"), MAKEINT(-1));
      }
      wire->send(MakeRefEvent(Cmd("NDrs"), answer));
    } else if (c == kPackageFinder && hasExtension("pfnd")) {
      RefVar request(UnflattenBytes(event.data));
      RefVar name(IsFrame(request) ? GetFrameSlot(request, MakeSymbol("packageName")) : (Ref)NILREF);
      bool installed = IsString(name) && UTF8FromString(name) == "Installed Package";
      wire->send(DockEvent::Make(installed ? Cmd("pkya") : Cmd("pkno")));
    } else if (c == kDRequestToInstall) {
      wire->send(DockEvent::MakeValue(kDResult, 0));
    } else if (c == kDRemovePackage) {
      RefVar name(UnflattenBytes(event.data));
      mRemoved.push_back(IsString(name) ? UTF8FromString(name) : std::string());
      wire->send(DockEvent::MakeValue(kDResult, 0));
    } else if (c == kDLoadPackage) {
      ++mPackagesLoaded;
      mPackageSize = event.data.size();
      wire->send(DockEvent::MakeValue(kDResult, 0));
    } else if (c == kDRequestToRestore || c == kDSourceVersion || c == kDSetSoupSignature) {
      wire->send(DockEvent::MakeValue(kDResult, 0));
    } else if (c == kDDeleteSoup) {
      mRestored.push_back("deletesoup " + currentSoup);
      wire->send(DockEvent::MakeValue(kDResult, 0));
    } else if (c == kDCreateSoup) {
      // the length of the name, the name (big-endian UniChars), the indexes
      uint32_t length = event.data.size() >= 4 ? event.longAt(0) : 0;
      std::u16string name;
      for (size_t i = 4; i + 1 < 4 + length && i + 1 < event.data.size(); i += 2) {
        char16_t ch = (char16_t)(event.data[i] << 8 | event.data[i + 1]);
        if (ch == 0)
          break;
        name.push_back(ch);
      }
      createdSoups.insert(UTF8FromUTF16(name));
      mRestored.push_back("createsoup " + UTF8FromUTF16(name));
      wire->send(DockEvent::MakeValue(kDResult, 0));
    } else if (c == kDAddEntryWithUniqueID) {
      RefVar entry(UnflattenBytes(event.data));
      RefVar id(IsFrame(entry) ? GetFrameSlot(entry, SYMA(_uniqueId)) : (Ref)NILREF);
      mRestored.push_back("entry " + currentSoup + " " + (ISINT(id) ? std::to_string(RINT(id)) : std::string("?")));
      wire->send(DockEvent::MakeValue(kDResult, 0));
    } else if (c == kDRequestToSync) {
      wire->send(DockEvent::MakeValue(kDResult, 0));
    } else if (c == kDGetSyncOptions) {
      RefVar options(AllocateFrame());
      SetFrameSlot(options, MakeSymbol("packages"), TRUEREF);
      SetFrameSlot(options, MakeSymbol("stores"), MakeStores());
      wire->send(MakeRefEvent(kDSyncOptions, options));
    } else if (c == kDLastSyncTime) {
      wire->send(DockEvent::MakeValue(kDCurrentTime, 50000000 + inOptions.round));
    } else if (c == kDGetAppNames) {
      RefVar apps(MakeArray(0));
      for (const FakeSoup & soup : soups) {
        RefVar app(AllocateFrame());
        if (!soup.appClass.empty())
          SetFrameSlot(app, SYMA(class), MakeSymbol(soup.appClass.c_str()));
        SetFrameSlot(app, SYMA(name), MakeStringFromUTF8(soup.app));
        RefVar names(MakeArray(1));
        SetArraySlot(names, 0, MakeStringFromUTF8(soup.name));
        SetFrameSlot(app, SYMA(soups), names);
        AddArraySlot(apps, app);
      }
      wire->send(MakeRefEvent(kDAppNames, apps));
    } else if (c == kDGetSoupInfo) {
      RefVar soupDef(AllocateFrame());
      SetFrameSlot(soupDef, SYMA(name), MakeStringFromUTF8(currentSoup));
      SetFrameSlot(soupDef, MakeSymbol("userDescr"), MakeStringFromUTF8(currentSoup + " soup"));
      SetFrameSlot(soupDef, MakeSymbol("ownerAppName"), MakeStringFromUTF8(currentSoup));
      RefVar info(AllocateFrame());
      SetFrameSlot(info, MakeSymbol("soupDef"), soupDef);
      wire->send(MakeRefEvent(kDSoupInfo, info));
    } else if (c == kDGetIndexDescription) {
      wire->send(MakeRefEvent(kDIndexDescription, RefVar(MakeArray(0))));
    } else if (c == kDGetChangedInfo || c == kDGetChangedIndex) {
      wire->send(DockEvent::MakeValue(kDResult, 0));      // unchanged
    } else if (c == kDSendSoup || c == kDBackupSoup) {
      // round 2: only Names changed (101 gone, 102 new)
      int64_t lastId = c == kDBackupSoup ? event.value() : -1;
      bool dirty = c == kDSendSoup || inOptions.round == 1 || currentSoup == "Names";
      if (!dirty) {
        wire->send(DockEvent::Make(kDSoupNotDirty));
        continue;
      }
      std::vector<int64_t> others;
      for (const FakeSoup & soup : soups)
        if (soup.name == currentSoup)
          for (ArrayIndex i = 0; i < Length(soup.entries); ++i) {
            RefVar entry(GetArraySlot(soup.entries, i));
            int64_t id = RINT(GetFrameSlot(entry, SYMA(_uniqueId)));
            if (id > lastId)
              wire->send(MakeRefEvent(kDEntry, entry));
            else
              others.push_back(id);
          }
      if (c == kDBackupSoup && !others.empty()) {
        // the ids of the rest: a base, then n (base + n) and -n (n more)
        std::sort(others.begin(), others.end());
        wire->send(DockEvent::MakeValue(kDSetBaseID, (int32_t)others[0]));
        std::vector<uint8_t> codes;
        auto put16 = [&](int v) { codes.push_back((uint8_t)(v >> 8)); codes.push_back((uint8_t)v); };
        for (size_t i = 0; i < others.size();) {
          size_t run = 1;
          while (i + run < others.size() && others[i + run] == others[i] + (int64_t)run)
            ++run;
          put16((int)(others[i] - others[0]));
          if (run > 1)
            put16(-(int)(run - 1));
          i += run;
        }
        put16(0x8000);
        wire->send(DockEvent::MakeData(kDBackupIDs, std::move(codes)));
      }
      wire->send(DockEvent::Make(kDBackupSoupDone));
    } else if (c == kDSetSoupInfo) {
      RefVar info(UnflattenBytes(event.data));
      if (IsFrame(info) && ISINT(GetFrameSlot(info, MakeSymbol("NCKLastBackupTime"))))
        mSoupInfos.push_back(currentSoup);
      wire->send(DockEvent::MakeValue(kDResult, 0));
    } else if (c == kDDeleteEntries) {
      for (uint32_t i = 0, count = event.data.size() >= 4 ? event.longAt(0) : 0; i < count; ++i)
        mDeleted.push_back((int64_t)event.longAt(4 + 4 * i));
      wire->send(DockEvent::MakeValue(kDResult, 0));
    } else if (c == kDAddEntry) {
      RefVar entry(UnflattenBytes(event.data));
      RefVar title(IsFrame(entry) ? GetFrameSlot(entry, SYMA(title)) : (Ref)NILREF);
      RefVar id(IsFrame(entry) ? GetFrameSlot(entry, SYMA(_uniqueId)) : (Ref)NILREF);
      if (inOptions.newton1 && ISINT(id) && RINT(id) >= 0 && RINT(id) < 100000) {
        // NewtonOS 1's restore: a backed-up entry, with its id
        mRestored.push_back("entry " + currentSoup + " " + std::to_string(RINT(id)));
      } else {
        mAdded.push_back(IsString(title) ? UTF8FromString(title) : std::string("?"));
      }
      // NewtonOS 1: a result only
      if (inOptions.newton1)
        wire->send(DockEvent::MakeValue(kDResult, 0));
      else
        wire->send(DockEvent::MakeValue(kDAddedID, 1000 + (int32_t)mAdded.size() - 1));
    } else if (c == kDCallGlobalFunction) {
      size_t end = 0;
      RefVar function(UnflattenBytes(event.data, 0, &end));
      RefVar args(UnflattenBytes(event.data, end));
      std::string name = IsSymbol(function) ? UTF8FromSymbol(function) : std::string();
      RefVar first(IsArray(args) && Length(args) > 0 ? GetArraySlot(args, 0) : (Ref)NILREF);
      RefVar result;
      if (name == "GetUserConfig")
        result = IsSymbol(first) && UTF8FromSymbol(first) == "hexade" ? (Ref)NILREF : MAKEINT(12290);
      else if (name == "Time")
        result = MAKEINT(50000000 + inOptions.round);
      else if (name == "Gestalt") {
        result = AllocateFrame();
        SetFrameSlot(result, MakeSymbol("ROMversionString"), MakeStringFromUTF8("2.1 (717006)"));
        SetFrameSlot(result, MakeSymbol("patchVersion"), MAKEINT(1));
        SetFrameSlot(result, MakeSymbol("manufactureDate"), MAKEINT(48000000));
        SetFrameSlot(result, MakeSymbol("CPUtype"), MakeSymbol("strongARM"));
        SetFrameSlot(result, MakeSymbol("CPUspeed"), MakeReal(161.9));
      } else
        result = AllocateFrame();
      wire->send(MakeRefEvent(kDCallResult, result));
    } else {
      wire->send(DockEvent::MakeValue(kDResult, kDockErrProtocolError));
    }
  }
}

} // namespace ncx
