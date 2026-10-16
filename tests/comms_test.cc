/*
 File: comms_test.cc

 Tests of NCX's comms layer (PORTING.md Phase 3) against the fake Newton
 (FakeNewton.h), which runs as a program of its own (ncx-fake-newton: the
 object system belongs to one thread, the dock's):

   ncx_comms_test <section> <ncx-fake-newton>
       section: events, mnp, plain, password, listener, package, or all

 Prints "ok <what>" or "FAIL <what>" per check; exit 0 if all are ok.
 */

#include "base/Core.h"
#include "base/NewtonStrings.h"
#include "FakeProcess.h"
#include "comms/DockEvent.h"
#include "comms/Handshake.h"
#include "comms/Listener.h"
#include "comms/Session.h"

#include "RSSymbols.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace ncx;

namespace {

int gFailed = 0;
int gPort = 0;
std::string gFakeNewton;        // the program

void Check(bool inOk, const std::string & inWhat)
{
  std::printf("%s %s\n", inOk ? "ok     " : "FAIL   ", inWhat.c_str());
  if (!inOk)
    ++gFailed;
}

/** Ports below 40000: Windows hands out 49152 and up to programs on its own,
    and CI runners reserve blocks there (a listener couldn't bind). */
int NextPort()
{
  if (gPort == 0)
    gPort = 20000 + (int)(getpid() % 4000) * 4;
  return gPort++;
}


/*-- events ------------------------------------------------------------------*/

void TestEvents()
{
  Check(Cmd("rtdk") == 0x7274646B && CmdName(0x7274646B) == "rtdk" && CmdName(0x01020304) == "....", "Cmd, CmdName");
  DockEvent result = DockEvent::MakeValue(kDResult, -28012);
  std::vector<uint8_t> bytes = result.encode();
  const uint8_t expected[] = { 'n','e','w','t','d','o','c','k', 'd','r','e','s', 0,0,0,4, 0xFF,0xFF,0x92,0x94 };
  Check(bytes == std::vector<uint8_t>(expected, expected + sizeof(expected)), "encode: a result of -28012");
  DockEvent odd = DockEvent::MakeData(kDSetCurrentSoup, "\0N\0a\0m\0e\0s", 10);
  Check(odd.encode().size() == 16 + 12 && odd.length == 10, "encode: 10 bytes padded to 12");
  Check(result.describe() == "dres [4] -28012", "describe: \"" + result.describe() + "\"");

  // a stream: garbage, the result, the padded event, an open-ended call, a
  // result; fed a byte at a time and all at once
  std::vector<uint8_t> stream = { 0x00, 'n', 'e', 'w', 0x16 };     // garbage, a false start
  for (const DockEvent & e : { result, odd }) {
    std::vector<uint8_t> b = e.encode();
    stream.insert(stream.end(), b.begin(), b.end());
  }
  DockEvent call = DockEvent::MakeData(kDCallGlobalFunction, kIndeterminateLength, { 1, 2, 3, 'n', 'e', 'w', 4, 5 });
  std::vector<uint8_t> b = call.encode();
  stream.insert(stream.end(), b.begin(), b.end());
  b = result.encode();
  stream.insert(stream.end(), b.begin(), b.end());
  for (int pass = 0; pass < 2; ++pass) {
    DockEventParser parser;
    std::vector<DockEvent> events;
    if (pass == 0)
      for (uint8_t c : stream) parser.feed(&c, 1, events);
    else
      parser.feed(stream.data(), stream.size(), events);
    bool ok = events.size() == 4
           && events[0].command == kDResult && events[0].value() == -28012
           && events[1].command == kDSetCurrentSoup && events[1].data.size() == 10 && events[1].length == 10
           && events[2].command == kDCallGlobalFunction && events[2].length == kIndeterminateLength
           && events[2].data == std::vector<uint8_t>({ 1, 2, 3, 'n', 'e', 'w', 4, 5 })
           && events[3].command == kDResult;
    Check(ok, pass == 0 ? "parse: a byte at a time (garbage, padding, open-ended)" : "parse: all at once");
  }
}


/*-- the fake Newton, a program of its own (FakeProcess.h) --------------------*/

typedef FakeProcess Fake;

void StartFake(Fake & ioFake, int inPort, const std::string & inOptions)
{
  ioFake.start(gFakeNewton, inPort, inOptions);
}

void FinishFake(Fake & ioFake)
{
  ioFake.finish();
}


/*-- a session against the fake Newton ---------------------------------------*/

struct Desktop
{
  bool docked = false;
  DockedNewton newton;
  std::string failure;
  int failureCode = 0;
};

/** The desktop's side: listen on inTargets, dock, run inSession, disconnect. */
template <typename Work>
Desktop RunDesktop(const std::vector<std::string> & inTargets, const std::string & inPassword, Work inWork,
                   std::string * outDescription = nullptr)
{
  Desktop desktop;
  Listener listener;
  std::string error;
  if (!listener.start(inTargets, error)) {
    desktop.failure = "can't listen: " + error;
    return desktop;
  }
  std::unique_ptr<Connection> connection = listener.waitForConnection(20000, error);
  if (!connection) {
    desktop.failure = "no connection: " + error;
    return desktop;
  }
  if (outDescription)
    *outDescription = connection->describe();
  EventQueue queue(std::move(connection));
  Session session(queue);
  try {
    desktop.newton = Handshake(session, inPassword, DockAppInfo(), 20);
    desktop.docked = true;
    inWork(session);
    session.send(kDDisconnect);
  } catch (const DockFailure & failure) {
    desktop.failure = failure.what();
    desktop.failureCode = failure.code;
  }
  queue.close();
  return desktop;
}


void TestSession(bool inPlain)
{
  InitCore();
  std::string kind = inPlain ? "plain TCP: " : "MNP: ";
  int port = NextPort();
  std::string password = "secret";
  Fake fake;
  StartFake(fake, port, std::string(inPlain ? "--plain " : "") + "--password " + password);

  std::string description;
  int storeCount = 0, soupCount = 0, idCount = 0, cursorCount = 0;
  std::string storeName, soupName, entryName, firstName, secondName, lastName = "?";
  int32_t userFont = 0;
  Desktop desktop = RunDesktop({ "tcp:" + std::to_string(port) }, password, [&](Session & session) {
    session.send(kDDesktopInControl);
    RefVar stores(session.getAllStores());
    storeCount = IsArray(stores) ? (int)Length(stores) : -1;
    if (storeCount > 0) {
      storeName = UTF8FromString(GetFrameSlot(GetArraySlot(stores, 0), SYMA(name)));
      session.setCurrentStore(GetArraySlot(stores, 0), false);
    }
    RefVar soups(session.getAllSoups());
    if (IsArray(soups) && Length(soups) == 2) {
      soupCount = (int)Length(GetArraySlot(soups, 0));
      soupName = UTF8FromString(GetArraySlot(GetArraySlot(soups, 0), 0));
    }
    session.setCurrentSoup(MakeStringFromUTF8("Names"));
    RefVar ids(session.getEntryIds());
    idCount = IsArray(ids) ? (int)Length(ids) : -1;
    RefVar entry(session.getEntry(101));
    entryName = UTF8FromString(GetFrameSlot(GetFrameSlot(entry, SYMA(name)), MakeSymbol("first")));
    {
      std::unique_ptr<Cursor> cursor = session.query(MakeStringFromUTF8("Names"), AllocateFrame());
      cursorCount = cursor->countEntries();
      firstName = UTF8FromString(GetFrameSlot(GetFrameSlot(cursor->entry(), SYMA(name)), MakeSymbol("first")));
      secondName = UTF8FromString(GetFrameSlot(GetFrameSlot(cursor->next(), SYMA(name)), MakeSymbol("first")));
      lastName = ISNIL(cursor->next()) ? "nil" : "an entry";
    }   // cfre
    RefVar font(session.getUserFont());
    userFont = ISINT(font) ? (int32_t)RVALUE(font) : -1;
    session.send(kDOperationDone);
  }, &description);
  FinishFake(fake);

  Check(desktop.docked && desktop.newton.name == "Ada Lovelace" && desktop.newton.protocolVersion == kDanteProtocolVersion
        && desktop.newton.info.fMachineType == 0x10003000 && desktop.newton.info.fROMVersion == 0x00020002,
        kind + "the handshake: name, protocol, machine, ROM " + desktop.failure);
  Check(description.compare(0, inPlain ? 3 : 3, inPlain ? "TCP" : "MNP") == 0, kind + "the listener chose it: " + description);
  Check(storeCount == 1 && storeName == "Internal", kind + "getAllStores, setCurrentStore");
  Check(soupCount == 2 && soupName == "Names", kind + "getAllSoups (two objects in an event)");
  Check(idCount == 2 && entryName == "Charles", kind + "setCurrentSoup, getEntryIds, getEntry");
  Check(cursorCount == 2 && firstName == "Ada" && secondName == "Charles" && lastName == "nil",
        kind + "query: countEntries, entry, next, next at the end");
  Check(userFont == 12290, kind + "callGlobalFunction (an event of no set length)");
  Check(fake.exitCode == 0, kind + "the fake Newton saw 'disc' " + fake.why);
  const std::vector<std::string> & r = fake.received;
  Check(r.size() > 6 && r[0] == "dock" && r[1] == "dinf" && r[2] == "wicn" && r[3] == "stim" && r[4] == "pass" && r.back() == "disc",
        kind + "the desktop's events in order: dock, dinf, wicn, stim, pass, ..., disc");
}


void TestPassword()
{
  InitCore();
  int port = NextPort();
  Fake fake;
  StartFake(fake, port, "--password secret");
  Desktop desktop = RunDesktop({ "tcp:" + std::to_string(port) }, "wrong", [](Session &) { });
  FinishFake(fake);
  int retries = 0;
  for (const std::string & c : fake.received)
    retries += c == "pwbd";
  Check(!desktop.docked && desktop.failureCode == kDockErrBadPasswordError && retries == 2 && fake.exitCode != 0,
        "a wrong password: two retries, then refused (" + desktop.failure + ")");
}


void TestListener()
{
  InitCore();
  int quiet = NextPort(), used = NextPort();
  Fake fake;
  StartFake(fake, used, "--plain");
  std::string description;
  Desktop desktop = RunDesktop({ "tcp:" + std::to_string(quiet), "tcp:" + std::to_string(used), "serial:/dev/no-such-port" },
                               "", [](Session &) { }, &description);
  FinishFake(fake);
  Check(desktop.docked && fake.exitCode == 0 && description.find(std::to_string(used)) != std::string::npos,
        "two ports and a missing serial port: the one the Newton used (" + description + ")" + desktop.failure);
  Listener again;
  std::string error;
  Check(again.start({ "tcp:" + std::to_string(quiet) }, error), "the other port was closed (listening there again works)");
}


void TestPackage()
{
  InitCore();
  int port = NextPort();
  std::filesystem::path file = std::filesystem::temp_directory_path() / ("ncx-test-" + std::to_string(port) + ".pkg");
  {
    std::ofstream out(file, std::ios::binary);
    for (int i = 0; i < 50000; ++i)
      out.put((char)(i * 7));
  }
  Fake fake;
  StartFake(fake, port, "");
  Progress progress;
  int32_t result = -1;
  Desktop desktop = RunDesktop({ "tcp:" + std::to_string(port) }, "", [&](Session & session) {
    session.sendPackage(file, &progress);
    result = session.receiveResult();
  });
  FinishFake(fake);
  std::filesystem::remove(file);
  Check(desktop.docked && result == 0 && fake.packageSize == 50000 && fake.exitCode == 0,
        "sendPackage: 50000 bytes over MNP, the Newton's result " + desktop.failure);
  Check(progress.total() == 50000 && progress.completed() == 50000, "sendPackage: the progress went to the end");
}

} // namespace


int main(int argc, char * argv[])
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::string section = argc > 1 ? argv[1] : "all";
  gFakeNewton = argc > 2 ? argv[2] : "ncx-fake-newton";
  struct { const char * name; void (*test)(); } sections[] = {
    { "events", TestEvents }, { "mnp", [] { TestSession(false); } }, { "plain", [] { TestSession(true); } },
    { "password", TestPassword }, { "listener", TestListener }, { "package", TestPackage } };
  bool found = false;
  for (auto & s : sections)
    if (section == "all" || section == s.name) {
      s.test();
      found = true;
    }
  if (!found) {
    std::fprintf(stderr, "no section \"%s\"\n", section.c_str());
    return 2;
  }
  std::printf("%s\n", gFailed ? "FAILED" : "all ok");
  return gFailed ? 1 : 0;
}
