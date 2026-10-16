/*
 File: main.cc

 ncx-cli: NCX without a window, for tests and debugging.

   ncx-cli --version                 the version
   ncx-cli --selftest                check the libraries (exit 0 if all is well)
   ncx-cli info [options]            wait for a Newton, dock, print what it
                                     is, its stores and soups, disconnect

 Options for info:
   --target <target>   listen here (more than once for several): tcp-all:<port>,
                       tcp:<port>, serial:<device>[@<bps>], einstein-pipes.
                       Default: tcp-all:<the TCPIPPort preference, 3679> and
                       einstein-pipes (macOS)
   --password <text>   the Newton's password (default: the preference)
   --wait <seconds>    give up waiting for a Newton after this long
   --log <level>       log to stderr: 1 protocol, 2 entries, 3 everything
 */

#include "base/Core.h"
#include "base/Log.h"
#include "base/NewtonStrings.h"
#include "base/Prefs.h"
#include "comms/Handshake.h"
#include "comms/Listener.h"
#include "comms/Session.h"

#include "RSSymbols.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int Usage()
{
  std::fprintf(stderr,
    "Usage: ncx-cli --version | --selftest\n"
    "       ncx-cli info [--target <target>]... [--password <text>] [--wait <seconds>] [--log <level>]\n");
  return 2;
}


std::string Text(RefArg inObj)
{
  if (IsString(inObj))
    return ncx::UTF8FromString(inObj);
  if (ISINT(inObj))
    return std::to_string(RVALUE(inObj));
  return "-";
}


int Info(int argc, char * argv[])
{
  std::vector<std::string> targets;
  std::string password = ncx::Prefs::App().getString(ncx::pref::kPassword);
  int waitSeconds = -1;
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--target" && i + 1 < argc)
      targets.push_back(argv[++i]);
    else if (arg == "--password" && i + 1 < argc)
      password = argv[++i];
    else if (arg == "--wait" && i + 1 < argc)
      waitSeconds = std::atoi(argv[++i]);
    else if (arg == "--log" && i + 1 < argc)
      ncx::SetLogLevel(std::atoi(argv[++i]));
    else
      return Usage();
  }
  if (targets.empty()) {
    targets.push_back("tcp-all:" + std::to_string(ncx::Prefs::App().tcpPort()));
#if defined(__APPLE__)
    targets.push_back("einstein-pipes");
#endif
  }

  ncx::InitCore();
  ncx::Listener listener;
  std::string error;
  if (!listener.start(targets, error)) {
    std::fprintf(stderr, "ncx-cli: can't listen: %s\n", error.c_str());
    return 1;
  }
  for (const std::string & where : listener.listening())
    std::fprintf(stderr, "Waiting for a Newton on %s\n", where.c_str());
  std::unique_ptr<ncx::Connection> connection = listener.waitForConnection(waitSeconds < 0 ? -1 : waitSeconds * 1000, error);
  if (!connection) {
    std::fprintf(stderr, "ncx-cli: %s\n", error.empty() ? "no Newton came" : error.c_str());
    return 1;
  }
  std::fprintf(stderr, "Connected: %s\n", connection->describe().c_str());

  ncx::EventQueue queue(std::move(connection));
  ncx::Session session(queue);
  try {
    ncx::DockedNewton newton = ncx::Handshake(session, password, ncx::DockAppInfo(), 60);
    std::printf("Newton:   %s\n", newton.name.empty() ? "(no name)" : newton.name.c_str());
    std::printf("Machine:  %s\n", ncx::MachineName(newton.info.fMachineType).c_str());
    std::printf("ROM:      %u.%u%s\n", newton.info.fROMVersion >> 16, newton.info.fROMVersion & 0xFFFF,
                newton.newton1 ? " (NewtonOS 1)" : "");
    std::printf("RAM:      %u KB\n", newton.info.fRAMSize / 1024);
    std::printf("Screen:   %u x %u\n", newton.info.fScreenWidth, newton.info.fScreenHeight);
    std::printf("Protocol: %u\n", newton.protocolVersion);
    if (!newton.newton1) {
      session.send(ncx::kDDesktopInControl);
      RefVar stores(session.getAllStores());
      for (ArrayIndex i = 0, count = IsArray(stores) ? Length(stores) : 0; i < count; ++i) {
        RefVar store(GetArraySlot(stores, i));
        std::printf("Store:    %s (%s), signature %s, %s of %s bytes used\n",
                    Text(GetFrameSlot(store, SYMA(name))).c_str(), Text(GetFrameSlot(store, SYMA(kind))).c_str(),
                    Text(GetFrameSlot(store, SYMA(signature))).c_str(),
                    Text(GetFrameSlot(store, MakeSymbol("usedSize"))).c_str(),
                    Text(GetFrameSlot(store, MakeSymbol("totalSize"))).c_str());
        session.setCurrentStore(store, false);
        RefVar soups(session.getAllSoups());
        if (IsArray(soups) && Length(soups) == 2) {
          RefVar names(GetArraySlot(soups, 0));
          std::printf("Soups:   ");
          for (ArrayIndex k = 0; k < Length(names); ++k)
            std::printf(" %s%s", Text(GetArraySlot(names, k)).c_str(), k + 1 < Length(names) ? "," : "");
          std::printf("\n");
        }
      }
      session.send(ncx::kDOperationDone);
    }
    session.send(ncx::kDDisconnect);
  } catch (const ncx::DockFailure & failure) {
    std::fprintf(stderr, "ncx-cli: %s (%d)\n", failure.what(), failure.code);
    return 1;
  }
  queue.close();
  return 0;
}

} // namespace


int main(int argc, char * argv[])
{
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("ncx-cli %s\n", ncx::Version());
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "--selftest") == 0) {
    std::string report;
    bool ok = ncx::SelfTest(report);
    std::fputs(report.c_str(), stdout);
    return ok ? 0 : 1;
  }
  if (argc >= 2 && std::strcmp(argv[1], "info") == 0)
    return Info(argc, argv);
  return Usage();
}
