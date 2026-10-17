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
                                     a request (2048; 0: all at once)
   ncx-cli backup [--folder <folder>] [--delete | --only-add] [options]
                                     back up the Newton into its
                                     .newtondevice file in the folder (the
                                     app's: NCX_DATA_DIR, or as NCX keeps them);
                                     entries the Newton has that the file
                                     doesn't: a few are deleted there, more
                                     only if asked (on the terminal, or
                                     --delete; --only-add leaves them)
   ncx-cli restore [--folder <folder>] [--to <store>] [--from <store>]
               [--apps <a,b,...>] [--packages] [--yes] [options]
                                     restore the Newton from its backup in
                                     the folder (as backup): to its store
                                     (Internal), from a backed-up one of the
                                     kind, those apps (all) and the packages;
                                     without --yes it only says what
   ncx-cli add [--folder <folder>] [--store <store>] [<soup>...] [options]
                                     send the entries made on the desktop
                                     (imported) in its backup in the folder
                                     (as backup) to the Newton now, of those
                                     soups (all), on that store (all)
   ncx-cli delete [--folder <folder>] [--store <store>] <soup> <entry>...
               [options]
                                     delete entries (by _uniqueId or title)
                                     on the Newton and in its backup; in
                                     Packages: remove packages (by name)
   ncx-cli type <text>... [--file <file>] [options]
                                     type on the Newton (keyboard
                                     passthrough), where its insertion point
                                     is: the texts (a space between), or
                                     the file's text (UTF-8); \n is a return
   ncx-cli install <package>... [--replace] [options]
                                     install packages (on the default store);
                                     --replace: those of the same name too
   ncx-cli serve [--folder <folder>] [--backups <folder>] [options]
                                     dock and wait: the Newton browses the
                                     folder (home) and installs from it, or
                                     backs up (into --backups, as backup);
                                     it disconnects when done
   ncx-cli rom <file> [--size <bytes>] [--address <addr>] [--block <bytes>]
               [--resume] [options]
                                     the Newton's ROM (8 MB from physical 0;
                                     or --size bytes from --address), --block
                                     bytes a request (2048), --resume after
                                     what the file has
   ncx-cli device <file> [--check]   a .newtondevice file: its Newton, and
                                     how many soups and entries per store;
                                     --check: that every entry can be read
                                     (and packages are packages)
   ncx-cli convert <nbku> <file>     a Newton Connection 1 backup (.nbku) as
                                     a new .newtondevice file

 Options for the commands that dock (info, screenshot, backup, restore,
 add, delete, type, install, serve, rom):
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
#include "dock/Backup.h"
#include "dock/DockController.h"
#include "dock/FileBrowser.h"
#include "dock/Keyboard.h"
#include "dock/Packages.h"
#include "dock/Restore.h"
#include "dock/MemoryDump.h"
#include "dock/Screenshot.h"
#include "dock/SoupEditor.h"
#include "model/BackupImport.h"
#include "model/DeviceFile.h"
#include "model/DeviceList.h"
#include "model/Restore.h"
#include "model/NewtonModel.h"
#include "platform/AppFolders.h"

#include "RSSymbols.h"

#include <FL/Fl_PNG_Image.H>

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

#if defined(_WIN32)
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif
#include <string>
#include <vector>

namespace {

int Usage()
{
  std::fprintf(stderr,
    "Usage: ncx-cli --version | --selftest\n"
    "       ncx-cli info [<dock options>]\n"
    "       ncx-cli screenshot <file.png> [--block <bytes>] [<dock options>]\n"
    "       ncx-cli backup [--folder <folder>] [--delete | --only-add] [<dock options>]\n"
    "       ncx-cli restore [--folder <folder>] [--to <store>] [--from <store>] [--apps <a,b,...>] [--packages] [--yes]\n"
    "               [<dock options>]\n"
    "       ncx-cli add [--folder <folder>] [--store <store>] [<soup>...] [<dock options>]\n"
    "       ncx-cli delete [--folder <folder>] [--store <store>] <soup> <entry>... [<dock options>]\n"
    "       ncx-cli type <text>... [--file <file>] [<dock options>]\n"
    "       ncx-cli install <package>... [--replace] [<dock options>]\n"
    "       ncx-cli serve [--folder <folder>] [--backups <folder>] [<dock options>]\n"
    "       ncx-cli rom <file> [--size <bytes>] [--address <addr>] [--block <bytes>] [--resume] [<dock options>]\n"
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


uint32_t Number(const char * inText)
{
  return (uint32_t)std::strtoul(inText, nullptr, 0);     // 0x... too
}


int ROM(int argc, char * argv[])
{
  // its own options: --size, --address, --block, --resume
  uint32_t size = 0, address = 0, block = ncx::MemoryReader::kBlock;
  bool resume = false;
  std::vector<char *> args(argv, argv + 2);
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--size" && i + 1 < argc)
      size = Number(argv[++i]);
    else if (arg == "--address" && i + 1 < argc)
      address = Number(argv[++i]);
    else if (arg == "--block" && i + 1 < argc)
      block = Number(argv[++i]);
    else if (arg == "--resume")
      resume = true;
    else
      args.push_back(argv[i]);
  }
  if (block < 256 || block > 16384 || (block & (block - 1)) || address % 4 || size % 4) {
    std::fprintf(stderr, "ncx-cli: --block: a power of 2 from 256 to 16384; --address, --size: multiples of 4\n");
    return 2;
  }
  ncx::DockController::Settings settings;
  std::vector<std::string> rest;
  if (!DockOptions((int)args.size(), args.data(), 2, settings, rest) || rest.size() != 1)
    return Usage();
  std::filesystem::path file = ncx::PathFromUTF8(rest[0]);
  return Dock(settings, [&](ncx::DockController & dock) {
    uint32_t bytes = size ? size : ncx::ROMSize(dock.newton().info);
    ncx::Progress progress;
    // progress now and then, on the dock thread (that's where it is)
    std::atomic<bool> done { false };
    std::thread reporter([&] {
      auto started = std::chrono::steady_clock::now();
      int64_t last = -1;
      while (!done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        int64_t completed = progress.completed();
        if (completed == last || progress.total() <= 0)
          continue;
        last = completed;
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::fprintf(stderr, "\r%lld of %lld KB, %.1f KB/s   ", (long long)completed / 1024,
                     (long long)progress.total() / 1024, seconds > 0 ? completed / 1024.0 / seconds : 0.0);
      }
    });
    int error = ncx::DumpMemory(dock, address, bytes, file, resume, block, &progress);
    done = true;
    reporter.join();
    std::fprintf(stderr, "\n");
    if (error != 0) {
      std::fprintf(stderr, "ncx-cli: %s isn't complete (%d); --resume goes on\n", rest[0].c_str(), error);
      return 1;
    }
    std::printf("%s: %u bytes from 0x%08X\n", rest[0].c_str(), bytes, address);
    return 0;
  });
}


int Install(int argc, char * argv[])
{
  bool replace = false;
  std::vector<char *> args(argv, argv + 2);
  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "--replace") == 0)
      replace = true;
    else
      args.push_back(argv[i]);
  }
  ncx::DockController::Settings settings;
  std::vector<std::string> rest;
  if (!DockOptions((int)args.size(), args.data(), 2, settings, rest) || rest.empty())
    return Usage();
  std::vector<std::filesystem::path> files;
  for (const std::string & name : rest) {
    ncx::PackageFile package;
    if (!ncx::ReadPackageFile(ncx::PathFromUTF8(name), package)) {
      std::fprintf(stderr, "ncx-cli: %s isn't a Newton package\n", name.c_str());
      return 1;
    }
    files.push_back(ncx::PathFromUTF8(name));
  }
  return Dock(settings, [&](ncx::DockController & dock) {
    ncx::PackageInstaller installer(dock);
    installer.setReplace([replace](const std::string &) { return replace; });
    int failed = 0;
    for (const ncx::InstallResult & result : installer.install(files)) {
      std::string file = ncx::UTF8FromPath(result.file.filename());
      if (result.error != 0) {
        std::printf("%s: not installed (%d)\n", file.c_str(), result.error);
        ++failed;
      } else if (result.skipped) {
        std::printf("%s: \"%s\" is installed already (--replace replaces it)\n", file.c_str(), result.name.c_str());
      } else {
        std::printf("%s: \"%s\" installed\n", file.c_str(), result.name.c_str());
      }
    }
    return failed ? 1 : 0;
  });
}


/** Where backups go: --folder, or the app's folder (NCX_DATA_DIR). */
std::filesystem::path BackupFolder(const std::string & inFolder)
{
  return inFolder.empty() ? ncx::AppDataFolder() : ncx::PathFromUTF8(inFolder);
}


/** What to do about entries the Newton has that the file doesn't: ask (on
    the terminal), delete them, or only add. */
enum class DeletePolicy { kAsk, kDelete, kOnlyAdd };

ncx::Deletions Decide(const ncx::DeletionPlan & inPlan, DeletePolicy inPolicy)
{
  for (const std::string & soup : inPlan.incomplete)
    std::fprintf(stderr, "Warning: the backup of %s never finished; the file may be incomplete there, "
                 "nothing of it is deleted on the Newton\n", soup.c_str());
  if (inPlan.total == 0)
    return ncx::Deletions::kOnlyAdd;
  std::fprintf(stderr, "About to delete %zu entries on the Newton that the backup doesn't have:", inPlan.total);
  for (const ncx::DeletionPlan::Soup & soup : inPlan.soups)
    std::fprintf(stderr, " %s %zu of %zu;", soup.name.c_str(), soup.ids.size(), soup.onNewton);
  std::fprintf(stderr, "\n");
  if (inPolicy == DeletePolicy::kAsk && isatty(fileno(stdin))) {
    std::fprintf(stderr, "Delete them [d], or only add [a]? ");
    int c = std::getchar();
    return c == 'd' || c == 'D' ? ncx::Deletions::kDelete : ncx::Deletions::kOnlyAdd;
  }
  if (inPolicy == DeletePolicy::kDelete)
    return ncx::Deletions::kDelete;
  std::fprintf(stderr, "Not deleted (--delete deletes them)\n");
  return ncx::Deletions::kOnlyAdd;
}


/** Back up the docked Newton into its file in inFolder (found by its id,
    or made); print what happened. 0 if all went well. */
int BackUp(ncx::DockController & ioDock, const std::filesystem::path & inFolder, bool inNewtonAsked,
           DeletePolicy inPolicy = DeletePolicy::kAsk)
{
  std::string error;
  std::unique_ptr<ncx::DeviceFile> file = ncx::OpenDeviceFileFor(inFolder, ioDock.newton().info, ioDock.newton().name, error);
  if (!file) {
    std::fprintf(stderr, "ncx-cli: %s\n", error.c_str());
    return 1;
  }
  if (!inNewtonAsked)
    ncx::UpdateDevice(ioDock, *file);
  ncx::Backup backup(ioDock, *file);
  backup.setDecide([inPolicy](const ncx::DeletionPlan & inPlan) { return Decide(inPlan, inPolicy); });
  ncx::Progress progress;
  int result = inNewtonAsked ? backup.answer(&progress) : backup.run(&progress);
  const ncx::BackupCounts & counts = backup.counts();
  std::printf("%s: %zu soups, %zu entries came, %zu removed, %zu deleted on the Newton, %zu sent to it%s\n",
              ncx::UTF8FromPath(file->path()).c_str(), counts.soups, counts.entries, counts.removed,
              counts.deletedOnNewton, counts.added, result == 0 ? "" : (" (" + std::to_string(result) + ")").c_str());
  return result == 0 ? 0 : 1;
}


int BackupCommand(int argc, char * argv[])
{
  std::string folder;
  DeletePolicy policy = DeletePolicy::kAsk;
  std::vector<char *> args(argv, argv + 2);
  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "--folder") == 0 && i + 1 < argc)
      folder = argv[++i];
    else if (std::strcmp(argv[i], "--delete") == 0)
      policy = DeletePolicy::kDelete;
    else if (std::strcmp(argv[i], "--only-add") == 0)
      policy = DeletePolicy::kOnlyAdd;
    else
      args.push_back(argv[i]);
  }
  ncx::DockController::Settings settings;
  std::vector<std::string> rest;
  if (!DockOptions((int)args.size(), args.data(), 2, settings, rest) || !rest.empty())
    return Usage();
  return Dock(settings, [&](ncx::DockController & dock) { return BackUp(dock, BackupFolder(folder), false, policy); });
}


std::vector<std::string> Split(const std::string & inText, char inSeparator)
{
  std::vector<std::string> parts;
  size_t start = 0;
  for (size_t at; (at = inText.find(inSeparator, start)) != std::string::npos; start = at + 1)
    parts.push_back(inText.substr(start, at - start));
  if (start < inText.size())
    parts.push_back(inText.substr(start));
  return parts;
}


/** The docked Newton's backup in inFolder (there must be one), up to date
    with its stores; null (and said why) if there is none. */
std::unique_ptr<ncx::DeviceFile> OpenBackupOf(ncx::DockController & ioDock, const std::string & inFolder)
{
  std::filesystem::path path = ncx::FindDeviceFile(BackupFolder(inFolder), ncx::DeviceId(ioDock.newton().info));
  std::string error;
  std::unique_ptr<ncx::DeviceFile> file = path.empty() ? nullptr : ncx::DeviceFile::Open(path, ncx::DeviceFile::kReadWrite, error);
  if (!file) {
    std::fprintf(stderr, "ncx-cli: no backup of this Newton in %s %s\n", ncx::UTF8FromPath(BackupFolder(inFolder)).c_str(),
                 error.c_str());
    return nullptr;
  }
  ncx::UpdateDevice(ioDock, *file);
  return file;
}


int RestoreCommand(int argc, char * argv[])
{
  std::string folder, to, from, appList;
  bool packages = false, yes = false;
  std::vector<char *> args(argv, argv + 2);
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--folder" && i + 1 < argc)
      folder = argv[++i];
    else if (arg == "--to" && i + 1 < argc)
      to = argv[++i];
    else if (arg == "--from" && i + 1 < argc)
      from = argv[++i];
    else if (arg == "--apps" && i + 1 < argc)
      appList = argv[++i];
    else if (arg == "--packages")
      packages = true;
    else if (arg == "--yes")
      yes = true;
    else
      args.push_back(argv[i]);
  }
  ncx::DockController::Settings settings;
  std::vector<std::string> rest;
  if (!DockOptions((int)args.size(), args.data(), 2, settings, rest) || !rest.empty())
    return Usage();
  return Dock(settings, [&](ncx::DockController & dock) {
    std::unique_ptr<ncx::DeviceFile> file = OpenBackupOf(dock, folder);
    if (!file)
      return 1;
    // the Newton's stores now, as the file has them
    std::vector<ncx::StoreRecord> newtonStores;
    dock.setDesktopControl(true);
    RefVar stores(dock.session().getAllStores());
    dock.setDesktopControl(false);
    for (ArrayIndex i = 0, count = IsArray(stores) ? Length(stores) : 0; i < count; ++i) {
      RefVar signature(GetFrameSlot(RefVar(GetArraySlot(stores, i)), MakeSymbol("signature")));
      for (const ncx::StoreRecord & store : file->stores())
        if (ISINT(signature) && store.signature == RINT(signature))
          newtonStores.push_back(store);
    }
    std::vector<ncx::RestoreChoice> choices = ncx::RestoreChoices(*file, newtonStores);
    const ncx::RestoreChoice * target = nullptr;
    for (const ncx::RestoreChoice & choice : choices)
      if (!target && (to.empty() || choice.store.name == to))
        target = &choice;
    const ncx::RestoreFrom * source = nullptr;
    if (target)
      for (const ncx::RestoreFrom & candidate : target->sources)
        if (!source && (from.empty() || candidate.store.name == from))
          source = &candidate;
    if (!target || !source) {
      std::fprintf(stderr, "ncx-cli: nothing to restore to %s from %s\n", to.empty() ? "the Newton" : to.c_str(),
                   from.empty() ? "the backup" : from.c_str());
      return 1;
    }
    ncx::RestoreSelection selection;
    selection.target = target->store;
    selection.source = source->store;
    std::vector<std::string> wanted = Split(appList, ',');
    for (const ncx::AppRecord & app : source->apps)
      if (wanted.empty() || std::find(wanted.begin(), wanted.end(), app.name) != wanted.end())
        selection.apps.push_back(app);
    if (packages)
      selection.packages = source->packages;
    std::printf("Restoring to %s from %s:", selection.target.name.c_str(), selection.source.name.c_str());
    for (const ncx::AppRecord & app : selection.apps)
      std::printf(" %s;", app.name.c_str());
    std::printf(" %zu packages\n", selection.packages.size());
    if (!yes) {
      std::printf("Nothing restored: it replaces what is on the Newton (--yes restores)\n");
      return 1;
    }
    ncx::Restore restore(dock, *file);
    int result = restore.run(selection);
    const ncx::RestoreCounts & counts = restore.counts();
    std::printf("%zu soups, %zu entries, %zu packages restored%s\n", counts.soups, counts.entries, counts.packages,
                result == 0 ? "" : (" (" + std::to_string(result) + ")").c_str());
    return result == 0 ? 0 : 1;
  });
}


/** add and delete: their options; the arguments left in outRest. */
bool EditOptions(int argc, char * argv[], std::string & outFolder, std::string & outStore,
                 ncx::DockController::Settings & outSettings, std::vector<std::string> & outRest)
{
  std::vector<char *> args(argv, argv + 2);
  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "--folder") == 0 && i + 1 < argc)
      outFolder = argv[++i];
    else if (std::strcmp(argv[i], "--store") == 0 && i + 1 < argc)
      outStore = argv[++i];
    else
      args.push_back(argv[i]);
  }
  return DockOptions((int)args.size(), args.data(), 2, outSettings, outRest);
}


int AddCommand(int argc, char * argv[])
{
  std::string folder, storeName;
  ncx::DockController::Settings settings;
  std::vector<std::string> soupNames;
  if (!EditOptions(argc, argv, folder, storeName, settings, soupNames))
    return Usage();
  return Dock(settings, [&](ncx::DockController & dock) {
    std::unique_ptr<ncx::DeviceFile> file = OpenBackupOf(dock, folder);
    if (!file)
      return 1;
    size_t added = 0;
    int result = 0;
    for (const ncx::StoreRecord & store : ncx::SortedStores(*file)) {
      if (!storeName.empty() && store.name != storeName)
        continue;
      for (const ncx::SoupRecord & soup : file->soups(store.pk)) {
        if (!soupNames.empty() && std::find(soupNames.begin(), soupNames.end(), soup.name) == soupNames.end())
          continue;
        ncx::SoupEditor editor(dock, *file);
        int error = editor.add(soup.pk);
        if (editor.counts().added)
          std::printf("%s: %s: %zu entries sent\n", store.name.c_str(), soup.name.c_str(), editor.counts().added);
        added += editor.counts().added;
        if (error != 0) {
          std::printf("%s: %s: not sent (%d)\n", store.name.c_str(), soup.name.c_str(), error);
          result = 1;
        }
      }
    }
    std::printf("%zu entries made on the desktop sent to the Newton\n", added);
    return result;
  });
}


int DeleteCommand(int argc, char * argv[])
{
  std::string folder, storeName;
  ncx::DockController::Settings settings;
  std::vector<std::string> rest;
  if (!EditOptions(argc, argv, folder, storeName, settings, rest) || rest.size() < 2)
    return Usage();
  std::string soupName = rest[0];
  std::vector<std::string> wanted(rest.begin() + 1, rest.end());
  return Dock(settings, [&](ncx::DockController & dock) {
    std::unique_ptr<ncx::DeviceFile> file = OpenBackupOf(dock, folder);
    if (!file)
      return 1;
    // the soup on the store (the first that has it: Internal first)
    std::optional<ncx::SoupRecord> soup;
    for (const ncx::StoreRecord & store : ncx::SortedStores(*file))
      if (!soup && (storeName.empty() || store.name == storeName))
        soup = file->findSoup(store.pk, soupName);
    if (!soup) {
      std::fprintf(stderr, "ncx-cli: the backup has no soup %s%s\n", soupName.c_str(),
                   storeName.empty() ? "" : (" on " + storeName).c_str());
      return 1;
    }
    // the entries: by _uniqueId, or by title
    std::vector<int64_t> pks;
    std::vector<ncx::EntryRecord> entries = file->entries(soup->pk);
    for (const std::string & what : wanted) {
      char * end = nullptr;
      long long id = std::strtoll(what.c_str(), &end, 10);
      bool byId = !what.empty() && *end == 0;
      size_t found = 0;
      for (const ncx::EntryRecord & entry : entries)
        if (byId ? entry.uniqueId == id : entry.title && *entry.title == what) {
          pks.push_back(entry.pk);
          ++found;
        }
      if (!found) {
        std::fprintf(stderr, "ncx-cli: %s has no entry %s\n", soupName.c_str(), what.c_str());
        return 1;
      }
    }
    ncx::SoupEditor editor(dock, *file);
    int result = editor.remove(soup->pk, pks);
    const ncx::SoupEditCounts & counts = editor.counts();
    std::printf("%zu entries and %zu packages deleted on the Newton, %zu never sent deleted%s\n", counts.deleted,
                counts.packages, counts.fileOnly, result == 0 ? "" : (" (" + std::to_string(result) + ")").c_str());
    return result == 0 ? 0 : 1;
  });
}


int TypeCommand(int argc, char * argv[])
{
  std::string fileName;
  std::vector<char *> args(argv, argv + 2);
  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "--file") == 0 && i + 1 < argc)
      fileName = argv[++i];
    else
      args.push_back(argv[i]);
  }
  ncx::DockController::Settings settings;
  std::vector<std::string> rest;
  if (!DockOptions((int)args.size(), args.data(), 2, settings, rest) || (rest.empty() == fileName.empty()))
    return Usage();
  std::string text;
  if (!fileName.empty()) {
    std::ifstream in(ncx::PathFromUTF8(fileName), std::ios::binary);
    if (!in) {
      std::fprintf(stderr, "ncx-cli: can't read %s\n", fileName.c_str());
      return 1;
    }
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  } else {
    for (const std::string & part : rest)
      text += (text.empty() ? "" : " ") + part;
    // \n on the command line: a return
    for (size_t at; (at = text.find("\\n")) != std::string::npos;)
      text.replace(at, 2, "\n");
  }
  return Dock(settings, [&](ncx::DockController & dock) {
    ncx::Keyboard keyboard(dock);
    int error = keyboard.begin();
    if (error != 0) {
      std::fprintf(stderr, "ncx-cli: the Newton didn't start keyboard passthrough (%d)\n", error);
      return 1;
    }
    keyboard.send(text);
    keyboard.end();
    std::printf("%zu characters typed\n", ncx::NewtonKeyboardText(text).size());
    return 0;
  });
}


int Serve(int argc, char * argv[])
{
  // dock and wait: the Newton browses the folder (Dock's Install) and
  // disconnects when it's done
  std::filesystem::path folder = ncx::HomeFolder();
  std::string backups;
  std::vector<char *> args(argv, argv + 2);
  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "--folder") == 0 && i + 1 < argc)
      folder = ncx::PathFromUTF8(argv[++i]);
    else if (std::strcmp(argv[i], "--backups") == 0 && i + 1 < argc)
      backups = argv[++i];
    else
      args.push_back(argv[i]);
  }
  ncx::DockController::Settings settings;
  std::vector<std::string> rest;
  if (!DockOptions((int)args.size(), args.data(), 2, settings, rest) || !rest.empty())
    return Usage();
  ncx::InitCore();
  ncx::DockController dock;
  ncx::FileBrowser browser(dock);
  browser.setFolder(folder);
  browser.setInstalled([](const std::filesystem::path & inFile, int inError) {
    std::printf("%s: %s\n", ncx::UTF8FromPath(inFile.filename()).c_str(),
                inError == 0 ? "installed" : ("not installed (" + std::to_string(inError) + ")").c_str());
  });
  // Dock's Backup on the Newton
  dock.handle(ncx::kDRequestToSync, [&](ncx::DockController & ioDock, const ncx::DockEvent &) {
    BackUp(ioDock, BackupFolder(backups), true);
  });
  ncx::DockController::Events events;
  events.listening = [](const std::vector<std::string> & inWhere) {
    for (const std::string & where : inWhere)
      std::fprintf(stderr, "Waiting for a Newton on %s\n", where.c_str());
  };
  events.docked = [&](const ncx::DockedNewton & inNewton) {
    std::fprintf(stderr, "Docked: %s; the Newton can install and back up now\n", inNewton.name.c_str());
    // the Newton in its file, as the app does it when a Newton docks
    dock.post([&](ncx::DockController & ioDock) {
      std::string error;
      std::unique_ptr<ncx::DeviceFile> file = ncx::OpenDeviceFileFor(BackupFolder(backups), ioDock.newton().info,
                                                                     ioDock.newton().name, error);
      if (file)
        ncx::UpdateDevice(ioDock, *file);
      else
        std::fprintf(stderr, "ncx-cli: %s\n", error.c_str());
    });
  };
  dock.setEvents(events);
  std::string error;
  if (!dock.start(settings, error)) {
    std::fprintf(stderr, "ncx-cli: can't listen: %s\n", error.c_str());
    return 1;
  }
  dock.wait();
  if (dock.result() != 0) {
    std::fprintf(stderr, "ncx-cli: %s (%d)\n", dock.why().c_str(), dock.result());
    return 1;
  }
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
  if (argc >= 2 && std::strcmp(argv[1], "backup") == 0)
    return BackupCommand(argc, argv);
  if (argc >= 2 && std::strcmp(argv[1], "restore") == 0)
    return RestoreCommand(argc, argv);
  if (argc >= 2 && std::strcmp(argv[1], "add") == 0)
    return AddCommand(argc, argv);
  if (argc >= 2 && std::strcmp(argv[1], "delete") == 0)
    return DeleteCommand(argc, argv);
  if (argc >= 2 && std::strcmp(argv[1], "type") == 0)
    return TypeCommand(argc, argv);
  if (argc >= 2 && std::strcmp(argv[1], "install") == 0)
    return Install(argc, argv);
  if (argc >= 2 && std::strcmp(argv[1], "serve") == 0)
    return Serve(argc, argv);
  if (argc >= 2 && std::strcmp(argv[1], "rom") == 0)
    return ROM(argc, argv);
  if (argc >= 2 && std::strcmp(argv[1], "screenshot") == 0)
    return Screenshot(argc, argv);
  if (argc >= 2 && std::strcmp(argv[1], "device") == 0)
    return Device(argc, argv);
  if (argc >= 2 && std::strcmp(argv[1], "convert") == 0)
    return Convert(argc, argv);
  return Usage();
}
