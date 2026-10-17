/*
 File: dock_test.cc

 Unit tests of NCX's dock (PORTING.md Phase 5): the protocol extensions,
 the dock controller and its operations against the fake Newton
 (ncx-fake-newton, a process of its own). One section per ctest:

   ncx_dock_test <section> <ncx-fake-newton>   extensions, bits, controller,
                                               screenshot, onepiece, notoolkit,
                                               memory, packagefiles, install,
                                               browse, backup, deletions,
                                               restore, or all
   ncx_dock_test make-package <file> <name>    a package to try things on

 Prints "ok <what>" or "FAIL <what>" per check; exit 0 if all are ok.
 */

#include "FakeNewton.h"
#include "FakeProcess.h"

#include "base/Core.h"
#include "base/Log.h"
#include "base/Strings.h"
#include "model/DeviceFile.h"
#include "model/DeviceList.h"
#include "model/NewtonModel.h"
#include "model/Restore.h"
#include "platform/AppFolders.h"
#include "base/NewtonCall.h"
#include "base/NewtonStrings.h"
#include "comms/DockRefs.h"
#include "dock/Backup.h"
#include "dock/DockController.h"
#include "dock/Extensions.h"
#include "dock/FileBrowser.h"
#include "dock/Packages.h"
#include "dock/Restore.h"
#include "dock/MemoryDump.h"
#include "dock/Screenshot.h"
#include "dock/SoupEditor.h"

#include "RSSymbols.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace ncx;

namespace {

int gFailed = 0;
std::string gFakeNewton;

void Check(bool inOk, const std::string & inWhat)
{
  std::printf("%s %s\n", inOk ? "ok     " : "FAIL   ", inWhat.c_str());
  if (!inOk)
    ++gFailed;
}

/** A port of its own for each section, below 40000 (Windows). The sections
    run as processes of their own, at the same time: a base at random, a
    multiple of 40, the sections' offsets below 40. */
int Port(int inSection)
{
  static int base = 21000 + (int)(std::random_device()() % 450) * 40;
  return base + inSection;
}

/** Dock with a fake Newton (inOptions: its command line), run inWork on
    the dock thread, disconnect. The controller's result; the fake's
    report in ioFake. */
template <typename Work>
int DockWithFake(int inPort, const std::string & inOptions, FakeProcess & ioFake, Work inWork)
{
  ioFake.start(gFakeNewton, inPort, "--password test " + inOptions);
  DockController dock;
  DockController::Events events;
  events.docked = [&](const DockedNewton &) {
    dock.post([&](DockController & ioDock) { inWork(ioDock); });
    dock.disconnect();
  };
  dock.setEvents(events);
  DockController::Settings settings;
  settings.targets = { "tcp:" + std::to_string(inPort) };
  settings.password = "test";
  settings.once = true;
  settings.waitSeconds = 20;
  std::string error;
  if (!dock.start(settings, error)) {
    std::printf("        can't listen: %s\n", error.c_str());
    ioFake.finish();
    return -1;
  }
  dock.wait();
  ioFake.finish();
  if (dock.result() != 0)
    std::printf("        %s (%d)\n", dock.why().c_str(), dock.result());
  return dock.result();
}

bool Received(const FakeProcess & inFake, const char * inCommand)
{
  for (const std::string & command : inFake.received)
    if (command == inCommand)
      return true;
  return false;
}

/*-- extensions --------------------------------------------------------------*/

void TestExtensions()
{
  for (const char * id : { "NCss", "scrn" }) {
    std::vector<uint8_t> extension = Extension(Cmd(*(const char (*)[5])id));
    bool ok = extension.size() > 40 && std::memcmp(extension.data(), id, 4) == 0 && extension[4] == 2;  // NSOF 2
    if (ok) {
      // the function: a frame of class 'CodeBlock
      std::vector<uint8_t> nsof(extension.begin() + 4, extension.end());
      bool function = false;
      CallNewton([&] {
        RefVar code(UnflattenBytes(nsof));
        function = IsFrame(code) && NOTNIL(GetFrameSlot(code, MakeSymbol("instructions")));
      });
      ok = function;
    }
    Check(ok, std::string("extensions: ") + id + " is built in, a function");
  }
  Check(Extension(Cmd("none")).empty(), "extensions: no such extension");
}

/*-- bits --------------------------------------------------------------------*/

void TestBits()
{
  const uint8_t one[] = { 0xA5 };                         // 1 0 1 0 0 1 0 1
  std::vector<uint8_t> gray = GrayFromBits(one, 1, 8, 1, 1, 1);
  Check(gray == std::vector<uint8_t>({ 0, 255, 0, 255, 255, 0, 255, 0 }), "bits: 1 bit a pixel");
  const uint8_t two[] = { 0x1B };                         // 0 1 2 3
  gray = GrayFromBits(two, 1, 4, 1, 1, 2);
  Check(gray == std::vector<uint8_t>({ 255, 170, 85, 0 }), "bits: 2 bits a pixel");
  const uint8_t four[] = { 0x0F, 0x80, 0xFF, 0xFF };      // rows of 2 bytes, 3 pixels used
  gray = GrayFromBits(four, 4, 3, 2, 2, 4);
  Check(gray == std::vector<uint8_t>({ 255, 0, 119, 0, 0, 0 }), "bits: 4 bits a pixel, rows with padding");
  const uint8_t eight[] = { 0, 128, 255 };
  gray = GrayFromBits(eight, 3, 3, 1, 3, 8);
  Check(gray == std::vector<uint8_t>({ 255, 127, 0 }), "bits: 8 bits a pixel");
  gray = GrayFromBits(four, 2, 3, 2, 2, 4);
  Check(gray.size() == 6 && gray[3] == 255, "bits: too few bytes stay white");
}

/*-- controller --------------------------------------------------------------*/

void TestController()
{
  FakeProcess fake;
  int stores = -1;
  std::string name;
  int result = DockWithFake(Port(1), "", fake, [&](DockController & dock) {
    name = dock.newton().name;
    dock.setDesktopControl(true);
    RefVar all(dock.session().getAllStores());
    stores = IsArray(all) ? (int)Length(all) : -1;
    dock.setDesktopControl(false);
  });
  Check(result == 0 && fake.exitCode == 0, "controller: docked and disconnected " + fake.why);
  Check(name == "Ada Lovelace" && stores == 1, "controller: an operation on the dock thread");
  Check(Received(fake, "dsnc") && Received(fake, "gsto") && Received(fake, "opdn") && !fake.received.empty()
        && fake.received.back() == "disc", "controller: in control, the stores, done, disc");

  // a Newton exception in an operation fails it, not the session
  FakeProcess fake2;
  bool after = false;
  result = DockWithFake(Port(2), "", fake2, [&](DockController & dock) {
    try {
      CallNewton([] { GetFrameSlot(RefVar(MAKEINT(1)), MakeSymbol("x")); });
    } catch (const NewtonException &) {
      after = true;
    }
  });
  Check(result == 0 && after && fake2.exitCode == 0, "controller: a Newton exception is caught");

  // stop() while waiting for a Newton
  DockController dock;
  DockController::Settings settings;
  settings.targets = { "tcp:" + std::to_string(Port(3)) };
  std::string error;
  bool started = dock.start(settings, error);
  auto before = std::chrono::steady_clock::now();
  dock.stop();
  double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count();
  Check(started && seconds < 2 && !dock.isDocked(), "controller: stop() while listening");
}

/*-- screenshot --------------------------------------------------------------*/

void TestScreenshot()
{
  FakeProcess fake;
  Screenshot shot;
  int began = -1, took = -1;
  Progress progress;
  int result = DockWithFake(Port(4), "", fake, [&](DockController & dock) {
    ScreenshotSession screenshots(dock);
    began = screenshots.begin();
    if (began == 0)
      took = screenshots.take(shot, &progress);
    screenshots.end();
  });
  Check(result == 0 && began == 0 && took == 0, "screenshot: taken " + fake.why);
  Check(shot.width == 320 && shot.height == 480 && shot.depth == 4 && shot.gray.size() == 320 * 480,
        "screenshot: 320 x 480, 4 bits a pixel");
  bool pixels = shot.gray.size() == 320 * 480;
  for (int y = 0; pixels && y < 480; ++y)
    for (int x = 0; pixels && x < 320; ++x)
      pixels = shot.gray[(size_t)y * 320 + x] == 255 - ScreenPixel(x, y) * 17;
  Check(pixels, "screenshot: every pixel");
  Check(fake.largestBlock > 0 && fake.largestBlock <= (size_t)ScreenshotSession::kBlock,
        "screenshot: rows of at most 2048 bytes at a time (" + std::to_string(fake.largestBlock) + ")");
  Check(progress.total() == 76800 && progress.completed() == 76800, "screenshot: the progress");
  Check(fake.blankBlocks == 5, "screenshot: white rows not sent (" + std::to_string(fake.blankBlocks) + " of 40)");
  Check(fake.extensions == std::vector<std::string>({ "scrn", "NCss" }), "screenshot: the extensions sent");
}

void TestOnePiece()
{
  FakeProcess fake;
  Screenshot shot;
  int took = -1;
  int result = DockWithFake(Port(6), "", fake, [&](DockController & dock) {
    ScreenshotSession screenshots(dock);
    screenshots.setBlock(0);
    if (screenshots.begin() == 0)
      took = screenshots.take(shot);
    screenshots.end();
  });
  bool pixels = result == 0 && took == 0 && shot.gray.size() == 320 * 480;
  for (int y = 0; pixels && y < 480; ++y)
    for (int x = 0; pixels && x < 320; ++x)
      pixels = shot.gray[(size_t)y * 320 + x] == 255 - ScreenPixel(x, y) * 17;
  Check(pixels && fake.largestBlock == 0, "onepiece: the bits with the shot, no reads");
}

void TestNoToolkit()
{
  FakeProcess fake;
  int began = 0;
  int result = DockWithFake(Port(5), "--no-toolkit", fake, [&](DockController & dock) {
    ScreenshotSession screenshots(dock);
    began = screenshots.begin();
  });
  Check(result == 0 && began == kDockErrFileNotFound, "notoolkit: no Toolkit app, no screenshot");
  Check(Received(fake, "opdn"), "notoolkit: the desktop gave up control");
}

/*-- memory ------------------------------------------------------------------*/

std::vector<uint8_t> ReadFile(const std::filesystem::path & inFile)
{
  std::ifstream in(inFile, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool MatchesMemory(const std::vector<uint8_t> & inBytes, uint32_t inAddress)
{
  for (size_t i = 0; i < inBytes.size(); ++i)
    if (inBytes[i] != MemoryByte(inAddress + (uint32_t)i))
      return false;
  return true;
}

void TestMemory()
{
  std::filesystem::path file = std::filesystem::temp_directory_path() / ("ncx-memory-" + std::to_string(Port(7)) + ".bin");
  std::filesystem::remove(file);

  // 64 KB from 0: a uniform block among them (0x1000 ... 0x1FFF)
  FakeProcess fake;
  int dumped = -1;
  Progress progress;
  int result = DockWithFake(Port(7), "", fake, [&](DockController & dock) {
    dumped = DumpMemory(dock, 0, 65536, file, false, MemoryReader::kBlock, &progress);
  });
  std::vector<uint8_t> bytes = ReadFile(file);
  Check(result == 0 && dumped == 0 && bytes.size() == 65536 && MatchesMemory(bytes, 0), "memory: 64 KB, every byte " + fake.why);
  Check(progress.completed() == 65536 && fake.extensions == std::vector<std::string>({ "NDmp" }),
        "memory: the progress, the extension");

  // resume: after the whole blocks the file has
  std::filesystem::resize_file(file, 20000);        // 9 blocks and a part
  FakeProcess fake2;
  result = DockWithFake(Port(8), "", fake2, [&](DockController & dock) {
    dumped = DumpMemory(dock, 0, 65536, file, true, MemoryReader::kBlock);
  });
  bytes = ReadFile(file);
  Check(result == 0 && dumped == 0 && bytes.size() == 65536 && MatchesMemory(bytes, 0)
        && fake2.memoryRead == 65536 - 9 * 2048, "memory: resumed after 9 blocks (" + std::to_string(fake2.memoryRead) + " read)");

  // across a 1 MB window: two maps, a block cut at the window's end
  FakeProcess fake3;
  result = DockWithFake(Port(9), "", fake3, [&](DockController & dock) {
    dumped = DumpMemory(dock, 0xFF000 + 1024, 8192, file, false, MemoryReader::kBlock);
  });
  bytes = ReadFile(file);
  Check(result == 0 && dumped == 0 && bytes.size() == 8192 && MatchesMemory(bytes, 0xFF000 + 1024),
        "memory: across a 1 MB window");
  std::filesystem::remove(file);
}

/*-- packages ----------------------------------------------------------------*/

/** A package file: no parts, its name, inExtra bytes after it. */
void MakePackage(const std::filesystem::path & inFile, const std::string & inName, size_t inExtra = 1000,
                 const char * inSignature = "package0")
{
  std::u16string name = UTF16FromUTF8(inName);
  std::vector<uint8_t> nameBytes;
  for (char16_t c : name) {
    nameBytes.push_back((uint8_t)(c >> 8));
    nameBytes.push_back((uint8_t)c);
  }
  nameBytes.push_back(0);
  nameBytes.push_back(0);
  std::vector<uint8_t> bytes(56, 0);
  std::memcpy(bytes.data(), inSignature, 8);
  auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) bytes[at + i] = (uint8_t)(v >> (24 - 8 * i)); };
  bytes[30] = (uint8_t)(nameBytes.size() >> 8);    // name: offset 0, length
  bytes[31] = (uint8_t)nameBytes.size();
  put32(48, 56 + (uint32_t)nameBytes.size());       // the directory's size
  bytes.insert(bytes.end(), nameBytes.begin(), nameBytes.end());
  bytes.resize(bytes.size() + inExtra, 0x5A);
  put32(32, (uint32_t)bytes.size());
  std::ofstream out(inFile, std::ios::binary);
  out.write((const char *)bytes.data(), (std::streamsize)bytes.size());
}

/** The size of MakePackage's file. */
uint64_t PackageSize(const std::string & inName, size_t inExtra)
{
  return 56 + (UTF16FromUTF8(inName).size() + 1) * 2 + inExtra;
}

std::filesystem::path TempFolder(const std::string & inName)
{
  std::filesystem::path folder = std::filesystem::temp_directory_path() / ("ncx-dock-" + inName + "-" + std::to_string(Port(0)));
  std::filesystem::remove_all(folder);
  std::filesystem::create_directories(folder);
  return folder;
}

void TestPackageFiles()
{
  std::filesystem::path folder = TempFolder("pkgfiles");
  MakePackage(folder / "a.newtonpkg", "Gr\xC3\xBC\xC3\x9F" "e Package", 1000);
  MakePackage(folder / "b.pkg", "B", 10, "package1");
  MakePackage(folder / "c.pkg", "C", 10, "xar!\0\0\0\0");          // a Mac installer package
  MakePackage(folder / "d.txt", "D", 10);
  PackageFile package;
  Check(ReadPackageFile(folder / "a.newtonpkg", package) && package.name == "Gr\xC3\xBC\xC3\x9F" "e Package"
        && package.size == PackageSize(package.name, 1000), "packagefiles: the name and size (" + package.name + ")");
  Check(ReadPackageFile(folder / "b.pkg", package) && package.name == "B", "packagefiles: package1");
  Check(!ReadPackageFile(folder / "c.pkg", package), "packagefiles: not a Newton package");
  Check(IsNewtonPackage(folder / "a.newtonpkg") && IsNewtonPackage(folder / "b.pkg") && !IsNewtonPackage(folder / "c.pkg")
        && !IsNewtonPackage(folder / "d.txt"), "packagefiles: which are offered");
  std::filesystem::remove_all(folder);
}

void TestInstall()
{
  std::filesystem::path folder = TempFolder("install");
  MakePackage(folder / "new.newtonpkg", "New Package", 20000);
  MakePackage(folder / "old.newtonpkg", "Installed Package", 3000);
  std::vector<std::filesystem::path> files = { folder / "new.newtonpkg", folder / "old.newtonpkg" };

  for (bool replace : { false, true }) {
    FakeProcess fake;
    std::vector<InstallResult> results;
    Progress progress;
    std::vector<std::string> asked;
    int result = DockWithFake(Port(replace ? 11 : 10), "", fake, [&](DockController & dock) {
      PackageInstaller installer(dock);
      installer.setReplace([&](const std::string & inName) {
        asked.push_back(inName);
        return replace;
      });
      results = installer.install(files, &progress);
    });
    std::string how = replace ? " (replace)" : "";
    Check(result == 0 && results.size() == 2 && results[0].error == 0 && !results[0].skipped
          && results[0].name == "New Package", "install: a new package" + how + " " + fake.why);
    Check(results.size() == 2 && results[1].error == 0 && results[1].skipped == !replace
          && asked == std::vector<std::string>({ "Installed Package" }), "install: one installed already, asked" + how);
    Check(fake.packagesLoaded == (replace ? 2u : 1u)
          && fake.removed == (replace ? std::vector<std::string>({ "Installed Package" }) : std::vector<std::string>()),
          "install: sent " + std::to_string(fake.packagesLoaded) + ", removed " + std::to_string(fake.removed.size()) + how);
    Check(Received(fake, "rins") && Received(fake, "opdn") && fake.extensions == std::vector<std::string>({ "pfnd" }),
          "install: asked to install, pfnd, done" + how);
    Check(progress.total() == (int64_t)(PackageSize("New Package", 20000) + PackageSize("Installed Package", 3000))
          && progress.completed() == progress.total(),
          "install: the progress of all" + how);
  }
  std::filesystem::remove_all(folder);
}

void TestBrowse()
{
  std::filesystem::path folder = TempFolder("browse");
  MakePackage(folder / "a.newtonpkg", "A Package", 5000);
  MakePackage(folder / ".hidden.newtonpkg", "Hidden", 10);
  std::ofstream(folder / "b.txt") << "not a package";
  std::filesystem::create_directories(folder / "sub");
  MakePackage(folder / "sub" / "c.newtonpkg", "C Package", 10);

  FakeProcess fake;
  fake.start(gFakeNewton, Port(12), "--password test --browse-install");
  DockController dock;
  FileBrowser browser(dock);
  browser.setFolder(folder);
  std::filesystem::path installed;
  int installError = -1;
  browser.setInstalled([&](const std::filesystem::path & inFile, int inError) {
    installed = inFile;
    installError = inError;
  });
  DockController::Settings settings;
  settings.targets = { "tcp:" + std::to_string(Port(12)) };
  settings.password = "test";
  settings.once = true;
  settings.waitSeconds = 20;
  std::string error;
  bool started = dock.start(settings, error);
  dock.wait();
  fake.finish();
  Check(started && dock.result() == 0 && fake.exitCode == 0,
        "browse: the Newton browsed, installed, disconnected " + fake.why + " (" + std::to_string(dock.result()) + " "
        + dock.why() + ", fake " + std::to_string(fake.exitCode) + ")");
  auto saw = [&](const std::string & inLine) {
    return std::find(fake.browsed.begin(), fake.browsed.end(), inLine) != fake.browsed.end();
  };
  std::string folderName = UTF8FromPath(std::filesystem::absolute(folder).filename());
  Check(!fake.browsed.empty() && fake.browsed.front() == "path " + ComputerName() && saw("path " + folderName),
        "browse: the path, the computer first");
  Check(saw("file a.newtonpkg 1") && saw("file sub 2") && !saw("file b.txt 1") && !saw("file .hidden.newtonpkg 1"),
        "browse: the folder's packages and folders, nothing else");
  Check(saw("subfile c.newtonpkg"), "browse: into a folder and back");
  Check(saw("info Newton package " + std::to_string(PackageSize("A Package", 5000))), "browse: a file's info");
  Check(installed.filename() == "a.newtonpkg" && installError == 0 && fake.packagesLoaded == 1
        && fake.packageSize == PackageSize("A Package", 5000), "browse: the package installed");
  std::filesystem::remove_all(folder);
}

/*-- backup ------------------------------------------------------------------*/

std::vector<int64_t> SoupIds(DeviceFile & inFile, const std::string & inSoup)
{
  std::vector<int64_t> ids;
  for (const StoreRecord & store : inFile.stores())
    if (std::optional<SoupRecord> soup = inFile.findSoup(store.pk, inSoup))
      for (const EntryRecord & entry : inFile.entries(soup->pk))
        ids.push_back(entry.uniqueId);
  return ids;
}

void TestBackup()
{
  std::filesystem::path folder = TempFolder("backup");
  std::filesystem::path path;

  // round 1: a new file, all of it
  FakeProcess fake;
  BackupCounts counts;
  int backedUp = -1;
  Progress progress;
  int result = DockWithFake(Port(13), "--round 1", fake, [&](DockController & dock) {
    std::string error;
    std::unique_ptr<DeviceFile> file = OpenDeviceFileFor(folder, dock.newton().info, dock.newton().name, error);
    if (!file)
      return;
    path = file->path();
    UpdateDevice(dock, *file);
    Backup backup(dock, *file);
    backedUp = backup.run(&progress);
    counts = backup.counts();
  });
  Check(result == 0 && backedUp == 0 && counts.soups == 4 && counts.entries == 5,
        "backup: the first, into a new file (" + std::to_string(counts.soups) + " soups, " + std::to_string(counts.entries)
        + " entries) " + fake.why);
  Check(progress.total() == 4 && progress.completed() == 4, "backup: the progress, app by app");
  Check(Received(fake, "ssyn") && Received(fake, "gsyn") && Received(fake, "snds") && Received(fake, "opdn")
        && fake.soupInfos.size() == 4, "backup: asked, sync options, soups sent, their backup time");
  {
    std::string error;
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadOnly, error);
    Check(file != nullptr, "backup: the file " + error);
    if (!file)
      return;
    std::optional<DeviceRecord> device = file->device();
    Check(device && device->name == "Ada Lovelace" && device->osInfo == std::string("2.1 (717006)-1")
          && device->processor == std::string("161.9 MHz  StrongARM") && device->backupDate && device->backupError == 0,
          "backup: the device, as Gestalt said");
    std::vector<StoreRecord> stores = file->stores();
    std::vector<SoupRecord> soups = file->soups();
    Check(stores.size() == 1 && stores[0].totalSize == 1048576 && soups.size() == 4, "backup: the store and its soups");
    bool apps = true, times = true;
    for (const SoupRecord & soup : soups) {
      apps = apps && soup.appPk != 0;
      times = times && soup.lastBackupTime == 50000002;
    }
    Check(apps && times, "backup: every soup with its app and its backup time");
    Check(SoupIds(*file, "Names") == std::vector<int64_t>({ 100, 101 }) && SoupIds(*file, "Packages") == std::vector<int64_t>({ 300 }),
          "backup: the entries (in Packages only the packages)");
    std::optional<SoupRecord> names = file->findSoup(stores[0].pk, "Names");
    std::optional<EntryRecord> ada = names ? file->findEntry(names->pk, 100, false) : std::nullopt;
    Check(ada && ada->title == std::string("Ada Lovelace") && names->lastBackupId == 101, "backup: titles, lastBackupId");
  }

  // on the desktop: Ada deleted, a note made
  {
    std::string error;
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadWrite, error);
    if (!file)
      return;
    StoreRecord store = file->stores().at(0);
    std::optional<SoupRecord> names = file->findSoup(store.pk, "Names");
    std::optional<SoupRecord> notes = file->findSoup(store.pk, "Notes");
    if (!names || !notes)
      return;
    DeviceFile::Transaction transaction(*file);
    file->deleteEntry(file->findEntry(names->pk, 100, false)->pk);
    RefVar note(AllocateFrame());
    SetFrameSlot(note, SYMA(class), MakeSymbol("paragraph"));
    SetFrameSlot(note, SYMA(title), MakeStringFromUTF8("Made on the desktop"));
    AddImportedEntry(*file, *notes, note);
    transaction.commit();
  }

  // round 2: Babbage gone and Somerville new on the Newton; Ada (deleted
  // here) is 1 of the 2 Names entries there: not clear, so it asks
  FakeProcess fake2;
  DeletionPlan asked;
  int askedTimes = 0;
  result = DockWithFake(Port(14), "--round 2", fake2, [&](DockController & dock) {
    std::string error;
    std::unique_ptr<DeviceFile> file = OpenDeviceFileFor(folder, dock.newton().info, dock.newton().name, error);
    if (!file)
      return;
    UpdateDevice(dock, *file);
    Backup backup(dock, *file);
    backup.setDecide([&](const DeletionPlan & inPlan) {
      asked = inPlan;
      ++askedTimes;
      return Deletions::kDelete;
    });
    backedUp = backup.run();
    counts = backup.counts();
  });
  Check(askedTimes == 1 && asked.total == 1 && asked.soups.size() == 1 && asked.soups[0].name == "Names"
        && asked.soups[0].ids == std::vector<int64_t>({ 100 }) && asked.soups[0].onNewton == 2 && !asked.clear
        && asked.incomplete.empty(), "backup: asked once about deleting 1 of 2 Names entries");
  Check(result == 0 && backedUp == 0 && counts.soups == 4 && counts.entries == 1, "backup: the second " + fake2.why);
  Check(counts.removed == 1 && fake2.deleted == std::vector<int64_t>({ 100 }) && counts.deletedOnNewton == 1,
        "backup: gone from the Newton, gone from the file; deleted here, deleted there");
  Check(counts.added == 1 && fake2.added == std::vector<std::string>({ "Made on the desktop" }),
        "backup: the note made on the desktop sent to the Newton");
  {
    std::string error;
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadOnly, error);
    std::vector<int64_t> notes = file ? SoupIds(*file, "Notes") : std::vector<int64_t>();
    Check(file && SoupIds(*file, "Names") == std::vector<int64_t>({ 102 }) && notes == std::vector<int64_t>({ 200, 1000 })
          && file->rowCount("ZDEVICE") == 1, "backup: the file afterwards (the note with the Newton's id)");
  }
  std::filesystem::remove_all(folder);
}

/*-- deletions on the Newton --------------------------------------------------*/

/** A backup of the fake Newton into inFolder (round 1), Ada deleted here,
    inPrepare done to the file, then round 2 with inDecision: the plan it
    was asked about (asked: if it was), the counts, the fake's report. */
void BackupTwice(const std::filesystem::path & inFolder, int inPort, std::function<void(DeviceFile &)> inPrepare,
                 Deletions inDecision, DeletionPlan & outPlan, bool & outAsked, BackupCounts & outCounts,
                 FakeProcess & outFake)
{
  FakeProcess first;
  std::filesystem::path path;
  DockWithFake(inPort, "--round 1", first, [&](DockController & dock) {
    std::string error;
    std::unique_ptr<DeviceFile> file = OpenDeviceFileFor(inFolder, dock.newton().info, dock.newton().name, error);
    if (!file)
      return;
    path = file->path();
    UpdateDevice(dock, *file);
    Backup(dock, *file).run();
  });
  {
    std::string error;
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadWrite, error);
    if (!file)
      return;
    std::optional<SoupRecord> names = file->findSoup(file->stores().at(0).pk, "Names");
    DeviceFile::Transaction transaction(*file);
    file->deleteEntry(file->findEntry(names->pk, 100, false)->pk);
    if (inPrepare)
      inPrepare(*file);
    transaction.commit();
  }
  outAsked = false;
  DockWithFake(inPort + 1, "--round 2", outFake, [&](DockController & dock) {
    std::string error;
    std::unique_ptr<DeviceFile> file = OpenDeviceFileFor(inFolder, dock.newton().info, dock.newton().name, error);
    if (!file)
      return;
    Backup backup(dock, *file);
    backup.setDecide([&](const DeletionPlan & inPlan) {
      outAsked = true;
      return inDecision;
    });
    backup.run();
    outPlan = backup.deletions();
    outCounts = backup.counts();
  });
}

void TestDeletions()
{
  // "Only Add": Ada stays on the Newton
  {
    std::filesystem::path folder = TempFolder("onlyadd");
    DeletionPlan plan;
    bool asked = false;
    BackupCounts counts;
    FakeProcess fake;
    BackupTwice(folder, Port(17), nullptr, Deletions::kOnlyAdd, plan, asked, counts, fake);
    Check(asked && plan.total == 1 && fake.deleted.empty() && counts.deletedOnNewton == 0 && fake.exitCode == 0,
          "deletions: only add, nothing deleted on the Newton " + fake.why);
    std::filesystem::remove_all(folder);
  }
  // a soup whose backup never finished (no NCKLastBackupTime): a warning,
  // nothing of it deleted, whatever the answer
  {
    std::filesystem::path folder = TempFolder("incomplete");
    DeletionPlan plan;
    bool asked = false;
    BackupCounts counts;
    FakeProcess fake;
    BackupTwice(folder, Port(19), [](DeviceFile & ioFile) {
      std::optional<SoupRecord> names = ioFile.findSoup(ioFile.stores().at(0).pk, "Names");
      names->lastBackupTime = 0;
      ioFile.update(*names);
    }, Deletions::kDelete, plan, asked, counts, fake);
    Check(asked && plan.incomplete == std::vector<std::string>({ "Internal: Names" }) && plan.total == 0
          && fake.deleted.empty() && fake.exitCode == 0, "deletions: an unfinished soup: a warning, nothing deleted " + fake.why);
    std::filesystem::remove_all(folder);
  }
  // what's clear
  DeletionPlan plan;
  plan.soups.push_back({ "Internal", "Names", 1, { 1, 2 }, 100 });
  plan.total = 2;
  plan.assess();
  Check(plan.clear, "deletions: 2 of 100 is clear");
  plan.soups[0].onNewton = 8;
  plan.assess();
  Check(!plan.clear, "deletions: 2 of 8 isn't (more than a fifth)");
  plan.soups[0].onNewton = 1000;
  plan.soups[0].ids.assign(11, 1);
  plan.total = 11;
  plan.assess();
  Check(!plan.clear, "deletions: 11 isn't (more than 10)");
  plan.soups[0].ids.assign(2, 1);
  plan.total = 2;
  plan.incomplete.push_back("Internal: Notes");
  plan.assess();
  Check(!plan.clear, "deletions: nothing is clear with an unfinished soup");
}

/*-- restore -----------------------------------------------------------------*/

void TestRestore()
{
  std::filesystem::path folder = TempFolder("restore");
  std::filesystem::path path;

  // a backup to restore from, and a note made on the desktop
  FakeProcess fake;
  int result = DockWithFake(Port(15), "", fake, [&](DockController & dock) {
    std::string error;
    std::unique_ptr<DeviceFile> file = OpenDeviceFileFor(folder, dock.newton().info, dock.newton().name, error);
    if (!file)
      return;
    path = file->path();
    UpdateDevice(dock, *file);
    Backup(dock, *file).run();
  });
  {
    std::string error;
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadWrite, error);
    if (!file) {
      Check(false, "restore: the backup " + error + " " + fake.why);
      return;
    }
    std::optional<SoupRecord> notes = file->findSoup(file->stores().at(0).pk, "Notes");
    RefVar note(AllocateFrame());
    SetFrameSlot(note, SYMA(title), MakeStringFromUTF8("Made on the desktop"));
    DeviceFile::Transaction transaction(*file);
    AddImportedEntry(*file, *notes, note);
    transaction.commit();
  }

  // all of it back: its apps and its package
  FakeProcess fake2;
  int restored = -1;
  RestoreCounts counts;
  Progress progress;
  result = DockWithFake(Port(16), "", fake2, [&](DockController & dock) {
    std::string error;
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadWrite, error);
    if (!file)
      return;
    std::vector<RestoreChoice> choices = RestoreChoices(*file, file->stores());
    if (choices.empty() || choices[0].sources.empty())
      return;
    RestoreSelection selection;
    selection.target = choices[0].store;
    selection.source = choices[0].sources[0].store;
    selection.apps = choices[0].sources[0].apps;
    selection.packages = choices[0].sources[0].packages;
    Restore restore(dock, *file);
    restored = restore.run(selection, &progress);
    counts = restore.counts();
  });
  Check(result == 0 && restored == 0 && counts.soups == 3 && counts.entries == 5 && counts.packages == 1,
        "restore: 3 soups, 5 entries, a package (" + std::to_string(counts.soups) + ", " + std::to_string(counts.entries)
        + ", " + std::to_string(counts.packages) + ") " + fake2.why);
  Check(Received(fake2, "rrst") && Received(fake2, "sver") && Received(fake2, "ssto") && Received(fake2, "opdn"),
        "restore: asked, the source, the store, done");
  auto did = [&](const std::string & inLine) {
    return std::find(fake2.restored.begin(), fake2.restored.end(), inLine) != fake2.restored.end();
  };
  Check(did("deletesoup Names") && did("createsoup Names") && did("entry Names 100") && did("entry Names 101")
        && did("createsoup System") && did("entry Notes 200"), "restore: the soups afresh, the entries with their ids");
  Check(fake2.added == std::vector<std::string>({ "Made on the desktop" }) && !did("entry Notes 1070000001"),
        "restore: the note made on the desktop added, not with its id");
  Check(fake2.removed == std::vector<std::string>({ "Tetris" }) && fake2.packagesLoaded == 1,
        "restore: the package removed and installed again");
  Check(progress.total() == 4 && progress.completed() == 4, "restore: the progress, apps and packages");
  {
    std::string error;
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadOnly, error);
    Check(file && SoupIds(*file, "Notes") == std::vector<int64_t>({ 200, 1000 }), "restore: the note has the Newton's id now");
  }
  std::filesystem::remove_all(folder);
}

/*-- edit --------------------------------------------------------------------*/

void TestEdit()
{
  std::filesystem::path folder = TempFolder("edit");
  std::filesystem::path path;

  // a backup, then on the desktop: a note made, a name made
  FakeProcess fake;
  int result = DockWithFake(Port(10), "", fake, [&](DockController & dock) {
    std::string error;
    std::unique_ptr<DeviceFile> file = OpenDeviceFileFor(folder, dock.newton().info, dock.newton().name, error);
    if (!file)
      return;
    path = file->path();
    UpdateDevice(dock, *file);
    Backup(dock, *file).run();
  });
  int64_t notesPk = 0, namesPk = 0, packagesPk = 0, madeNamePk = 0, babbagePk = 0, tetrisPk = 0;
  {
    std::string error;
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadWrite, error);
    if (!file) {
      Check(false, "edit: the backup " + error + " " + fake.why);
      return;
    }
    int64_t store = file->stores().at(0).pk;
    std::optional<SoupRecord> notes = file->findSoup(store, "Notes");
    std::optional<SoupRecord> names = file->findSoup(store, "Names");
    std::optional<SoupRecord> packages = file->findSoup(store, "Packages");
    if (!notes || !names || !packages) {
      Check(false, "edit: the backup's soups");
      return;
    }
    notesPk = notes->pk;
    namesPk = names->pk;
    packagesPk = packages->pk;
    babbagePk = file->findEntry(namesPk, 101, false)->pk;
    tetrisPk = file->findEntry(packagesPk, 300, false)->pk;
    DeviceFile::Transaction transaction(*file);
    RefVar note(AllocateFrame());
    SetFrameSlot(note, SYMA(title), MakeStringFromUTF8("Made on the desktop"));
    AddImportedEntry(*file, *notes, note);
    RefVar name(AllocateFrame());
    SetFrameSlot(name, SYMA(title), MakeStringFromUTF8("Not sent"));
    madeNamePk = AddImportedEntry(*file, *names, name).pk;
    transaction.commit();
  }

  // docked: the note to the Newton; Babbage, the name not sent and Tetris
  // deleted
  FakeProcess fake2;
  int added = -1, deleted = -1, removed = -1;
  SoupEditCounts addCounts, deleteCounts, removeCounts;
  result = DockWithFake(Port(11), "", fake2, [&](DockController & dock) {
    std::string error;
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadWrite, error);
    if (!file)
      return;
    SoupEditor editor(dock, *file);
    added = editor.add(notesPk);
    addCounts = editor.counts();
    deleted = editor.remove(namesPk, { babbagePk, madeNamePk });
    deleteCounts = editor.counts();
    removed = editor.remove(packagesPk, { tetrisPk });
    removeCounts = editor.counts();
  });
  Check(result == 0 && added == 0 && addCounts.added == 1 && fake2.added == std::vector<std::string>({ "Made on the desktop" }),
        "edit: the note made on the desktop sent " + fake2.why);
  Check(deleted == 0 && deleteCounts.deleted == 1 && deleteCounts.fileOnly == 1
        && fake2.deleted == std::vector<int64_t>({ 101 }), "edit: Babbage deleted on the Newton, the name never sent not");
  Check(removed == 0 && removeCounts.packages == 1 && fake2.removed == std::vector<std::string>({ "Tetris" }),
        "edit: the package removed by name");
  Check(Received(fake2, "ssto") && Received(fake2, "ssou") && Received(fake2, "sdef") && Received(fake2, "opdn"),
        "edit: store, soup, the default store for packages, done");
  {
    std::string error;
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadOnly, error);
    Check(file && SoupIds(*file, "Notes") == std::vector<int64_t>({ 200, 1000 }) && SoupIds(*file, "Names") == std::vector<int64_t>({ 100 })
          && SoupIds(*file, "Packages").empty(), "edit: the file as the Newton is now");
  }
  std::filesystem::remove_all(folder);
}

} // namespace


int main(int argc, char * argv[])
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::string section = argc > 1 ? argv[1] : "all";
  if (section == "make-package" && argc == 4) {
    // a package to try things on (the ncx-cli install test)
    MakePackage(PathFromUTF8(argv[2]), argv[3], 4000);
    return 0;
  }
  gFakeNewton = argc > 2 ? argv[2] : "ncx-fake-newton";
  if (const char * level = std::getenv("NCX_LOG"))      // the dock's log, to stderr
    SetLogLevel(std::atoi(level));
  InitCore();
  struct { const char * name; void (*test)(); } sections[] = {
    { "extensions", TestExtensions }, { "bits", TestBits }, { "controller", TestController },
    { "screenshot", TestScreenshot }, { "onepiece", TestOnePiece }, { "notoolkit", TestNoToolkit },
    { "memory", TestMemory }, { "packagefiles", TestPackageFiles }, { "install", TestInstall },
    { "browse", TestBrowse }, { "backup", TestBackup }, { "deletions", TestDeletions }, { "restore", TestRestore },
    { "edit", TestEdit } };
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
