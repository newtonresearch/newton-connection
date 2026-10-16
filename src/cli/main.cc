/*
 File: main.cc

 ncx-cli: NCX without a window, for tests and debugging.

   ncx-cli --version                 the version
   ncx-cli --selftest                check the libraries (exit 0 if all is well)
   ncx-cli info [options]            wait for a Newton, dock, print what it
                                     is, its stores and soups, disconnect
   ncx-cli screenshot <file.png> [--block <bytes>] [options]
                                     the Newton's screen (it needs the
                                     Toolkit app), as a PNG; --block: bytes
                                     a request (2048)
   ncx-cli device <file> [--check]   a .newtondevice file: its Newton, and
                                     how many soups and entries per store;
                                     --check: that every entry can be read
                                     (and packages are packages)
   ncx-cli convert <nbku> <file>     a Newton Connection 1 backup (.nbku) as
                                     a new .newtondevice file

 Options for the commands that dock (info, screenshot):
   --target <target>   listen here (more than once for several): tcp-all:<port>,
                       tcp:<port>, serial:<device>[@<bps>], einstein-pipes.
                       Default: tcp-all:<the TCPIPPort preference, 3679> and
                       einstein-pipes (macOS)
   --password <text>   the Newton's password (default: the preference)
   --wait <seconds>    give up waiting for a Newton after this long
   --log <level>       log to stderr: 1 protocol, 2 entries, 3 everything
                       (with every MNP frame)
 */

#include "base/Core.h"
#include "base/Log.h"
#include "base/NewtonStrings.h"
#include "base/Prefs.h"
#include "comms/Handshake.h"
#include "comms/Session.h"
#include "dock/DockController.h"
#include "dock/Screenshot.h"
#include "model/BackupImport.h"
#include "model/DeviceFile.h"
#include "model/NewtonModel.h"
#include "platform/AppFolders.h"

#include "RSSymbols.h"

#include <FL/Fl_PNG_Image.H>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

int Usage()
{
  std::fprintf(stderr,
    "Usage: ncx-cli --version | --selftest\n"
    "       ncx-cli info [<dock options>]\n"
    "       ncx-cli screenshot <file.png> [--block <bytes>] [<dock options>]\n"
    "         dock options: [--target <target>]... [--password <text>] [--wait <seconds>] [--log <level>]\n"
    "       ncx-cli device <file.newtondevice> [--check]\n"
    "       ncx-cli convert <backup.nbku> <file.newtondevice>\n");
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


/** The options of the commands that dock (info, screenshot, ...): the
    dock's settings; the arguments that aren't options in outRest. False
    for an option it doesn't know. */
bool DockOptions(int argc, char * argv[], int inFirst, ncx::DockController::Settings & outSettings,
                 std::vector<std::string> & outRest)
{
  outSettings.password = ncx::Prefs::App().getString(ncx::pref::kPassword);
  outSettings.once = true;
  for (int i = inFirst; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--target" && i + 1 < argc)
      outSettings.targets.push_back(argv[++i]);
    else if (arg == "--password" && i + 1 < argc)
      outSettings.password = argv[++i];
    else if (arg == "--wait" && i + 1 < argc)
      outSettings.waitSeconds = std::atoi(argv[++i]);
    else if (arg == "--log" && i + 1 < argc)
      ncx::SetLogLevel(std::atoi(argv[++i]));
    else if (arg.size() > 2 && arg.compare(0, 2, "--") == 0)
      return false;
    else
      outRest.push_back(arg);
  }
  if (outSettings.targets.empty()) {
    outSettings.targets.push_back("tcp-all:" + std::to_string(ncx::Prefs::App().tcpPort()));
#if defined(__APPLE__)
    outSettings.targets.push_back("einstein-pipes");
#endif
  }
  return true;
}


/** Dock once, run inTask on the dock thread, disconnect. Exit code: 0 if
    the task returned 0 and the session ended as it should. */
int Dock(const ncx::DockController::Settings & inSettings, std::function<int(ncx::DockController &)> inTask)
{
  ncx::InitCore();
  ncx::DockController dock;
  int taskResult = 1;
  ncx::DockController::Events events;
  events.listening = [](const std::vector<std::string> & inWhere) {
    for (const std::string & where : inWhere)
      std::fprintf(stderr, "Waiting for a Newton on %s\n", where.c_str());
  };
  events.docked = [&](const ncx::DockedNewton &) {
    dock.post([&](ncx::DockController & ioDock) { taskResult = inTask(ioDock); });
    dock.disconnect();
  };
  events.status = [](const std::string & inText) {
    if (!inText.empty())
      std::fprintf(stderr, "%s\n", inText.c_str());
  };
  dock.setEvents(events);
  std::string error;
  if (!dock.start(inSettings, error)) {
    std::fprintf(stderr, "ncx-cli: can't listen: %s\n", error.c_str());
    return 1;
  }
  dock.wait();
  if (dock.result() != 0) {
    std::fprintf(stderr, "ncx-cli: %s (%d)\n", dock.why().c_str(), dock.result());
    return 1;
  }
  return taskResult;
}


int Info(int argc, char * argv[])
{
  ncx::DockController::Settings settings;
  std::vector<std::string> rest;
  if (!DockOptions(argc, argv, 2, settings, rest) || !rest.empty())
    return Usage();
  return Dock(settings, [](ncx::DockController & dock) {
    const ncx::DockedNewton & newton = dock.newton();
    std::printf("Newton:   %s\n", newton.name.empty() ? "(no name)" : newton.name.c_str());
    std::printf("Machine:  %s\n", ncx::MachineName(newton.info.fMachineType).c_str());
    std::printf("ROM:      %u.%u%s\n", newton.info.fROMVersion >> 16, newton.info.fROMVersion & 0xFFFF,
                newton.newton1 ? " (NewtonOS 1)" : "");
    std::printf("RAM:      %u KB\n", newton.info.fRAMSize / 1024);
    std::printf("Screen:   %u x %u\n", newton.info.fScreenWidth, newton.info.fScreenHeight);
    std::printf("Protocol: %u\n", newton.protocolVersion);
    if (newton.newton1)
      return 0;
    ncx::Session & session = dock.session();
    dock.setDesktopControl(true);
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
    dock.setDesktopControl(false);
    return 0;
  });
}


int Screenshot(int argc, char * argv[])
{
  // --block <bytes>: bytes a request (to try a Newton's link)
  int block = ncx::ScreenshotSession::kBlock;
  std::vector<char *> args(argv, argv + argc);
  for (size_t i = 2; i + 1 < args.size(); ++i)
    if (std::strcmp(args[i], "--block") == 0) {
      block = std::atoi(args[i + 1]);
      args.erase(args.begin() + (long)i, args.begin() + (long)i + 2);
      break;
    }
  ncx::DockController::Settings settings;
  std::vector<std::string> rest;
  if (!DockOptions((int)args.size(), args.data(), 2, settings, rest) || rest.size() != 1)
    return Usage();
  std::string file = rest[0];
  ncx::Screenshot shot;
  int result = Dock(settings, [&](ncx::DockController & dock) {
    ncx::ScreenshotSession screenshots(dock);
    screenshots.setBlock(block);
    int error = screenshots.begin();
    if (error == ncx::kDockErrFileNotFound) {
      std::fprintf(stderr, "ncx-cli: the Newton needs the Toolkit app for screenshots\n");
      return 1;
    }
    if (error == 0)
      error = screenshots.take(shot);
    screenshots.end();
    if (error != 0) {
      std::fprintf(stderr, "ncx-cli: no screenshot (%d)\n", error);
      return 1;
    }
    return 0;
  });
  if (result != 0)
    return result;
  if (fl_write_png(file.c_str(), shot.gray.data(), shot.width, shot.height, 1) != 0) {
    std::fprintf(stderr, "ncx-cli: can't write %s\n", file.c_str());
    return 1;
  }
  std::printf("%s: %d x %d, %d bits a pixel on the Newton\n", file.c_str(), shot.width, shot.height, shot.depth);
  return 0;
}


int Device(int argc, char * argv[])
{
  if (argc < 3 || argc > 4 || (argc == 4 && std::strcmp(argv[3], "--check") != 0))
    return Usage();
  bool check = argc == 4;
  ncx::InitCore();
  std::string error;
  std::unique_ptr<ncx::DeviceFile> file = ncx::DeviceFile::Open(ncx::PathFromUTF8(argv[2]), ncx::DeviceFile::kReadOnly, error);
  if (!file) {
    std::fprintf(stderr, "ncx-cli: %s\n", error.c_str());
    return 1;
  }
  std::printf("Newton:   %s (%s)\n", file->newtonName().c_str(), file->newtonId().c_str());
  if (std::optional<ncx::DeviceRecord> device = file->device()) {
    ncx::NewtonInfo info = ncx::NewtonInfoOf(*device);
    std::printf("Machine:  %s, %s\n", ncx::ManufacturerName(info).c_str(), ncx::MachineTypeName(info).c_str());
  }
  size_t soupCount = 0, entryCount = 0, unreadable = 0;
  for (const ncx::StoreRecord & store : ncx::SortedStores(*file)) {
    std::vector<ncx::SoupRecord> soups = file->soups(store.pk);
    size_t entries = 0;
    for (const ncx::SoupRecord & soup : soups) {
      entries += file->entryCount(soup.pk);
      if (check)
        for (const ncx::EntryRecord & entry : file->entries(soup.pk, true)) {
          RefVar frame(ncx::EntryFrame(entry));
          bool ok = IsFrame(frame);
          if (ok && entry.refClass == std::string("*package*")) {
            // a package's binary is a package: "package0" or "package1"
            RefVar package(GetFrameSlot(frame, MakeSymbol("pkgRef")));
            ok = IsBinary(package) && Length(package) > 8 && std::memcmp(BinaryData(package), "package", 7) == 0;
          }
          if (!ok) {
            ++unreadable;
            std::printf("Unread:   an entry of class %s, %zu bytes\n", entry.refClass ? entry.refClass->c_str() : "-",
                        entry.refData.size());
          }
        }
    }
    std::printf("Store:    %s: %zu soups, %zu entries\n", store.name.c_str(), soups.size(), entries);
    soupCount += soups.size();
    entryCount += entries;
  }
  std::printf("Total:    %zu soups, %zu entries\n", soupCount, entryCount);
  if (check)
    std::printf("Check:    %zu entries can't be read\n", unreadable);
  return unreadable ? 1 : 0;
}


int Convert(int argc, char * argv[])
{
  if (argc != 4)
    return Usage();
  ncx::InitCore();
  std::string error;
  std::unique_ptr<ncx::DeviceFile> file = ncx::ImportBackup(ncx::PathFromUTF8(argv[2]), error);
  if (!file) {
    std::fprintf(stderr, "ncx-cli: %s\n", error.c_str());
    return 1;
  }
  try {
    file->saveAs(ncx::PathFromUTF8(argv[3]));
  } catch (const ncx::ModelError & e) {
    std::fprintf(stderr, "ncx-cli: %s\n", e.what());
    return 1;
  }
  std::printf("%zu soups, %zu entries\n", file->rowCount("ZSOUP"), file->rowCount("ZENTRY"));
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
  if (argc >= 2 && std::strcmp(argv[1], "screenshot") == 0)
    return Screenshot(argc, argv);
  if (argc >= 2 && std::strcmp(argv[1], "device") == 0)
    return Device(argc, argv);
  if (argc >= 2 && std::strcmp(argv[1], "convert") == 0)
    return Convert(argc, argv);
  return Usage();
}
