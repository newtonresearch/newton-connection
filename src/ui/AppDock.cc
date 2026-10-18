/*
 File: AppDock.cc

 The app's dock (AppDock.h).
 */

#include "ui/AppDock.h"

#include "base/Log.h"
#include "base/Prefs.h"
#include "dock/Backup.h"
#include "dock/Card.h"
#include "dock/MemoryDump.h"
#include "dock/DockController.h"
#include "dock/FileBrowser.h"
#include "dock/Keyboard.h"
#include "dock/Packages.h"
#include "dock/SoupEditor.h"
#include "comms/Handshake.h"
#include "comms/Session.h"
#include "ui/Edits.h"
#include "model/DeviceList.h"
#include "platform/AppFolders.h"

#include <atomic>

namespace ncx {

struct AppDock::Impl
{
  Hooks hooks;
  std::unique_ptr<FileBrowser> browser;
  std::atomic<bool> newton1 { false };
  std::atomic<int> operations { 0 };          // posted, not ended
  mutable std::mutex lock;                    // progress
  std::shared_ptr<Progress> progress;
  std::unique_ptr<ScreenshotSession> screenshots;   // the dock thread's; while begun
  std::unique_ptr<Keyboard> keyboard;               // its handlers registered before the dock starts

  /** inWork as an operation of the session it is posted in: with a
      Progress, started and finished told; not run if that session ended
      before it could. */
  void post(DockController & ioDock, const std::string & inTitle,
            std::function<std::string(DockController &, Progress &, bool &, std::filesystem::path &)> inWork);
  void begin(const std::string & inTitle);
  void end(const std::string & inReport, bool inOK, const std::filesystem::path & inFile);
};


namespace {

/** The deletions a backup plans, for the user to decide. */
std::string DeletionText(const DeletionPlan & inPlan)
{
  std::string text;
  for (const std::string & soup : inPlan.incomplete)
    text += "The backup of " + soup + " never finished: the file may be incomplete there, "
            "nothing of it is deleted on the Newton.\n";
  if (inPlan.total > 0) {
    text += "The backup is about to delete " + std::to_string(inPlan.total)
          + " entries on the Newton that the backup file doesn't have:\n";
    for (const DeletionPlan::Soup & soup : inPlan.soups)
      text += "  " + soup.store + ", " + soup.name + ": " + std::to_string(soup.ids.size())
            + " of " + std::to_string(soup.onNewton) + "\n";
  }
  return text;
}


std::string BackupReport(const BackupCounts & inCounts, int inResult)
{
  std::string text = "Backed up " + std::to_string(inCounts.soups) + " soups: "
                   + std::to_string(inCounts.entries) + " entries came";
  if (inCounts.removed)
    text += ", " + std::to_string(inCounts.removed) + " removed";
  if (inCounts.deletedOnNewton)
    text += ", " + std::to_string(inCounts.deletedOnNewton) + " deleted on the Newton";
  if (inCounts.added)
    text += ", " + std::to_string(inCounts.added) + " sent to it";
  if (inResult == kDockErrAborted)
    text += " (cancelled)";
  else if (inResult != 0)
    text += " (error " + std::to_string(inResult) + ")";
  return text;
}


/** Back the docked Newton up into its file; what came of it. */
std::string BackUp(DockController & ioDock, const AppDock::Hooks & inHooks, bool inNewtonAsked, Progress & ioProgress,
                   bool & outOK, std::filesystem::path & outFile)
{
  std::string error;
  std::unique_ptr<DeviceFile> file = OpenDeviceFileFor(AppDataFolder(), ioDock.newton().info, ioDock.newton().name, error);
  if (!file) {
    outOK = false;
    return "Not backed up: " + error;
  }
  outFile = file->path();
  Backup backup(ioDock, *file);
  backup.setDecide([&inHooks](const DeletionPlan & inPlan) {
    std::string text = DeletionText(inPlan);
    if (inPlan.total == 0) {
      if (!text.empty())
        Log(kLogMinimum, "%s", text.c_str());
      return Deletions::kOnlyAdd;
    }
    return inHooks.confirmDeletions && inHooks.confirmDeletions(text) ? Deletions::kDelete : Deletions::kOnlyAdd;
  });
  int result = inNewtonAsked ? backup.answer(&ioProgress) : backup.run(&ioProgress);
  outOK = result == 0;
  return BackupReport(backup.counts(), result);
}

} // namespace


void AppDock::Impl::begin(const std::string & inTitle)
{
  std::shared_ptr<Progress> fresh = std::make_shared<Progress>();
  fresh->setDescription(inTitle);
  {
    std::lock_guard<std::mutex> guard(lock);
    progress = fresh;
  }
  if (hooks.started)
    hooks.started(inTitle);
}


void AppDock::Impl::end(const std::string & inReport, bool inOK, const std::filesystem::path & inFile)
{
  {
    std::lock_guard<std::mutex> guard(lock);
    progress.reset();
  }
  Log(kLogMinimum, "%s", inReport.c_str());
  if (hooks.finished)
    hooks.finished(inReport, inOK, inFile);
}


void AppDock::Impl::post(DockController & ioDock, const std::string & inTitle,
                         std::function<std::string(DockController &, Progress &, bool &, std::filesystem::path &)> inWork)
{
  unsigned session = ioDock.sessionNumber();
  ++operations;
  ioDock.post([this, session, inTitle, inWork](DockController & ioDock) {
    struct Count
    {
      std::atomic<int> & count;
      ~Count() { --count; }
    } count { operations };
    if (ioDock.sessionNumber() != session) {
      if (hooks.finished)
        hooks.finished(inTitle + ": not done, the Newton disconnected", false, {});
      return;
    }
    begin(inTitle);
    bool ok = true;
    std::filesystem::path file;
    std::string report;
    try {
      std::shared_ptr<Progress> progress;
      {
        std::lock_guard<std::mutex> guard(lock);
        progress = this->progress;
      }
      report = inWork(ioDock, *progress, ok, file);
    } catch (...) {
      end(inTitle + ": the connection broke", false, file);
      throw;      // the controller ends the session
    }
    end(report, ok, file);
    // NewtonOS 1 docks for one thing: done, it goes
    if (ioDock.newton().newton1)
      ioDock.disconnect();
  });
}


AppDock::AppDock() : mImpl(new Impl()), mDock(new DockController())
{
}


AppDock::~AppDock()
{
  stop();
}


bool AppDock::start(const Hooks & inHooks, std::string & outError, const std::vector<std::string> & inTargets)
{
  mImpl->hooks = inHooks;
  Impl & impl = *mImpl;
  DockController::Settings settings;
  settings.targets = inTargets.empty() ? DefaultTargets() : inTargets;
  settings.password = Prefs::App().getString(pref::kPassword);
  settings.keepTrying = true;

  // the Newton browses the desktop (Dock's Install), and backs itself up
  // (Dock's Backup)
  impl.keyboard.reset(new Keyboard(*mDock));
  impl.keyboard->setChanged([&impl](bool inOn) {
    if (impl.hooks.keyboardChanged)
      impl.hooks.keyboardChanged(inOn);
  });
  impl.browser.reset(new FileBrowser(*mDock));
  impl.browser->setFolder(HomeFolder());
  mDock->handle(kDRequestToSync, [&impl](DockController & ioDock, const DockEvent &) {
    ++impl.operations;
    struct Count
    {
      std::atomic<int> & count;
      ~Count() { --count; }
    } count { impl.operations };
    impl.begin("Backing up");
    bool ok = true;
    std::filesystem::path file;
    std::string report = BackUp(ioDock, impl.hooks, true, *impl.progress, ok, file);
    impl.end(report, ok, file);
  });

  DockController::Events events;
  events.listening = [&impl](const std::vector<std::string> & inWhere) {
    if (impl.hooks.listening)
      impl.hooks.listening(inWhere);
  };
  events.docked = [this, &impl](const DockedNewton & inNewton) {
    impl.newton1 = inNewton.newton1;
    // the Newton in its file first (the Cocoa app's do_GALS), then the UI
    mDock->post([&impl](DockController & ioDock) {
      std::string error;
      std::filesystem::path path;
      std::unique_ptr<DeviceFile> file = OpenDeviceFileFor(AppDataFolder(), ioDock.newton().info, ioDock.newton().name, error);
      if (file) {
        UpdateDevice(ioDock, *file, Prefs::App().getBool(pref::kSetNewtonTime));
        path = file->path();
      }
      if (impl.hooks.docked)
        impl.hooks.docked(ioDock.newton().name, path, ioDock.newton().newton1, error);
    });
  };
  events.undocked = [&impl](int inCode, const std::string & inWhy) {
    impl.screenshots.reset();
    if (impl.hooks.undocked)
      impl.hooks.undocked(inCode, inWhy);
  };
  events.status = [&impl](const std::string & inText) {
    if (impl.hooks.status)
      impl.hooks.status(inText);
  };
  mDock->setEvents(events);
  std::string error;
  bool listening = mDock->start(settings, error);
  if (!listening)
    Log(kLogMinimum, "dock: can't listen yet: %s", error.c_str());
  if (!mDock->isRunning()) {
    outError = error;
    return false;
  }
  return true;
}


std::vector<std::string> AppDock::DefaultTargets()
{
  Prefs & prefs = Prefs::App();
  std::vector<std::string> targets = { "tcp-all:" + std::to_string(prefs.tcpPort()) };
  std::string serial = prefs.getString(pref::kSerialPort);
  if (!serial.empty())
    targets.push_back("serial:" + serial + "@" + std::to_string(prefs.baudRate()));
#if defined(__APPLE__)
  targets.push_back("einstein-pipes");
#endif
  return targets;
}


void AppDock::stop()
{
  if (mDock)
    mDock->stop();
}


bool AppDock::docked() const
{
  return mDock->isDocked();
}


bool AppDock::newton1() const
{
  return mImpl->newton1;
}


bool AppDock::busy() const
{
  return mImpl->operations > 0;
}


std::shared_ptr<Progress> AppDock::progress() const
{
  std::lock_guard<std::mutex> guard(mImpl->lock);
  return mImpl->progress;
}


void AppDock::cancel()
{
  if (std::shared_ptr<Progress> progress = this->progress())
    progress->cancel();
}


void AppDock::backUp()
{
  Impl & impl = *mImpl;
  impl.post(*mDock, "Backing up", [&impl](DockController & ioDock, Progress & ioProgress, bool & outOK,
                                          std::filesystem::path & outFile) {
    return BackUp(ioDock, impl.hooks, false, ioProgress, outOK, outFile);
  });
}


void AppDock::install(const std::vector<std::filesystem::path> & inFiles)
{
  Impl & impl = *mImpl;
  std::string title = inFiles.size() == 1 ? "Installing " + UTF8FromPath(inFiles[0].filename())
                                          : "Installing " + std::to_string(inFiles.size()) + " packages";
  impl.post(*mDock, title, [&impl, inFiles](DockController & ioDock, Progress & ioProgress, bool & outOK,
                                            std::filesystem::path &) {
    PackageInstaller installer(ioDock);
    installer.setReplace([&impl](const std::string & inName) {
      return impl.hooks.replacePackage && impl.hooks.replacePackage(inName);
    });
    std::vector<InstallResult> results = installer.install(inFiles, &ioProgress);
    size_t installed = 0, skipped = 0;
    std::string failed;
    for (const InstallResult & result : results) {
      if (result.error != 0) {
        failed += (failed.empty() ? "" : ", ") + UTF8FromPath(result.file.filename())
                + " (" + std::to_string(result.error) + ")";
      } else if (result.skipped)
        ++skipped;
      else
        ++installed;
    }
    outOK = failed.empty();
    std::string report = "Installed " + std::to_string(installed) + (installed == 1 ? " package" : " packages");
    if (skipped)
      report += ", " + std::to_string(skipped) + " left as they were";
    if (!failed.empty())
      report += "; not installed: " + failed;
    return report;
  });
}


void AppDock::importFiles(const std::filesystem::path & inFile, int64_t inStorePk,
                          const std::vector<std::pair<std::filesystem::path, std::string>> & inItems)
{
  std::vector<ImportItem> items;
  for (const auto & item : inItems)
    items.push_back({ item.first, item.second });
  std::string title = items.size() == 1 ? "Importing " + UTF8FromPath(items[0].source.filename())
                                        : "Importing " + std::to_string(items.size()) + " files";
  mImpl->post(*mDock, title, [inFile, inStorePk, items](DockController & ioDock, Progress &, bool & outOK,
                                                         std::filesystem::path & outFile) {
    std::vector<int64_t> entries, soups;
    std::string report = ImportIntoFile(inFile, inStorePk, items, entries, soups, outOK);
    outFile = inFile;
    if (entries.empty())
      return report;
    // to the Newton, soup by soup
    std::string error;
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(inFile, DeviceFile::kReadWrite, error);
    if (!file) {
      outOK = false;
      return report + "; not sent to the Newton: " + error;
    }
    SoupEditor editor(ioDock, *file);
    int result = 0;
    for (int64_t soup : soups)
      if (result == 0)
        result = editor.add(soup, entries);
    if (result != 0) {
      outOK = false;
      return report + "; the Newton took " + std::to_string(editor.counts().added) + " (error " + std::to_string(result) + ")";
    }
    return report + ", sent to the Newton";
  });
}


void AppDock::restoreChoices(const std::filesystem::path & inFile,
                             std::function<void(const std::vector<RestoreChoice> &, const std::string &)> inChoices)
{
  unsigned session = mDock->sessionNumber();
  mDock->post([inFile, inChoices, session](DockController & ioDock) {
    if (ioDock.sessionNumber() != session) {
      inChoices({}, "the Newton disconnected");
      return;
    }
    std::string error;
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(inFile, DeviceFile::kReadOnly, error);
    if (!file) {
      inChoices({}, error);
      return;
    }
    // the Newton's stores now, as the file has them (ncx-cli restore); if
    // the connection breaks meanwhile, the UI hears of it (and the session
    // ends)
    struct Broken
    {
      const std::function<void(const std::vector<RestoreChoice> &, const std::string &)> & tell;
      bool told = false;
      ~Broken() { if (!told) tell({}, "the connection broke"); }
    } broken { inChoices };
    std::vector<StoreRecord> newtonStores;
    ioDock.setDesktopControl(true);
    RefVar stores(ioDock.session().getAllStores());
    ioDock.setDesktopControl(false);
    broken.told = true;
    std::vector<StoreRecord> backedUp = file->stores();
    for (ArrayIndex i = 0, count = IsArray(stores) ? Length(stores) : 0; i < count; ++i) {
      RefVar signature(GetFrameSlot(RefVar(GetArraySlot(stores, i)), MakeSymbol("signature")));
      for (const StoreRecord & store : backedUp)
        if (ISINT(signature) && store.signature == RINT(signature))
          newtonStores.push_back(store);
    }
    std::vector<RestoreChoice> choices = RestoreChoices(*file, newtonStores);
    inChoices(choices, choices.empty() ? std::string("nothing in the backup fits the Newton's stores") : std::string());
  });
}


void AppDock::restore(const std::filesystem::path & inFile, const RestoreSelection & inSelection)
{
  mImpl->post(*mDock, "Restoring", [inFile, inSelection](DockController & ioDock, Progress & ioProgress, bool & outOK,
                                                          std::filesystem::path & outFile) {
    std::string error;
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(inFile, DeviceFile::kReadWrite, error);
    if (!file) {
      outOK = false;
      return "Not restored: " + error;
    }
    outFile = inFile;
    Restore restore(ioDock, *file);
    int result = restore.run(inSelection, &ioProgress);
    const RestoreCounts & counts = restore.counts();
    outOK = result == 0;
    std::string report = "Restored " + std::to_string(counts.soups) + " soups, " + std::to_string(counts.entries)
                       + " entries, " + std::to_string(counts.packages) + " packages";
    if (result == kDockErrAborted)
      report += " (cancelled)";
    else if (result != 0)
      report += " (error " + std::to_string(result) + ")";
    return report;
  });
}


void AppDock::dumpROM(const std::filesystem::path & inFile, bool inResume)
{
  mImpl->post(*mDock, "Reading the ROM", [inFile, inResume](DockController & ioDock, Progress & ioProgress, bool & outOK,
                                                           std::filesystem::path &) -> std::string {
    Prefs & prefs = Prefs::App();
    uint32_t address = (uint32_t)prefs.getInt(pref::kROMAddress, 0) & ~3u;
    uint32_t size = (uint32_t)prefs.getInt(pref::kROMSize, 0) & ~3u;
    if (size == 0)
      size = ROMSize(ioDock.newton().info);
    int error = DumpMemory(ioDock, address, size, inFile, inResume, MemoryReader::kBlock, &ioProgress);
    outOK = error == 0;
    if (error == kDockErrAborted)
      return std::string("The ROM isn't complete (cancelled): Dump ROM again resumes");
    if (error != 0)
      return "The ROM isn't complete (" + std::to_string(error) + "): Dump ROM again resumes";
    return "Read the ROM: " + std::to_string(size) + " bytes into " + UTF8FromPath(inFile.filename());
  });
}


void AppDock::dumpCards(const std::filesystem::path & inFolder)
{
  mImpl->post(*mDock, "Reading the cards", [inFolder](DockController & ioDock, Progress & ioProgress, bool & outOK,
                                                      std::filesystem::path &) -> std::string {
    const DockedNewton & newton = ioDock.newton();
    MemoryReader reader(ioDock);
    int error = reader.begin();
    if (error != 0) {
      outOK = false;
      return "No cards read: the Newton didn't take the extension (" + std::to_string(error) + ")";
    }
    CardReader cards(reader);
    std::vector<CardSocket> sockets;
    error = cards.sockets(newton.info, sockets);
    if (error != 0 || sockets.empty()) {
      outOK = false;
      return error != 0 ? "No cards read (" + std::to_string(error) + ")"
                        : "No cards read: NCX doesn't know the sockets of a " + MachineName(newton.info.fMachineType);
    }
    std::string header = "Newton: " + newton.name + ", " + MachineName(newton.info.fMachineType);
    std::error_code ec;
    std::filesystem::create_directories(inFolder, ec);
    size_t read = 0;
    std::string report;
    outOK = true;
    for (const CardSocket & socket : sockets) {
      if (!socket.inserted)
        continue;
      CardDump dump;
      error = cards.dump(socket.socket, inFolder, header, 0, false, MemoryReader::kBlock, &ioProgress, dump);
      if (error != 0) {
        outOK = false;
        report += "; socket " + std::to_string(socket.socket) + ": " + cards.why() + " (" + std::to_string(error) + ")";
        if (error == kDockErrAborted)
          break;
        continue;
      }
      ++read;
    }
    return "Read " + std::to_string(read) + (read == 1 ? " card" : " cards") + " into " + UTF8FromPath(inFolder) + report;
  });
}


void AppDock::removeEntries(const std::filesystem::path & inFile, int64_t inSoupPk, const std::vector<int64_t> & inEntryPks)
{
  std::string title = "Deleting " + std::to_string(inEntryPks.size()) + (inEntryPks.size() == 1 ? " entry" : " entries");
  mImpl->post(*mDock, title, [inFile, inSoupPk, inEntryPks](DockController & ioDock, Progress &, bool & outOK,
                                                             std::filesystem::path & outFile) {
    std::string error;
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(inFile, DeviceFile::kReadWrite, error);
    if (!file) {
      outOK = false;
      return "Not deleted: " + error;
    }
    outFile = inFile;
    SoupEditor editor(ioDock, *file);
    int result = editor.remove(inSoupPk, inEntryPks);
    const SoupEditCounts & counts = editor.counts();
    outOK = result == 0;
    std::string report = "Deleted " + std::to_string(counts.deleted + counts.fileOnly)
                       + (counts.deleted + counts.fileOnly == 1 ? " entry" : " entries");
    if (counts.packages)
      report += ", removed " + std::to_string(counts.packages) + (counts.packages == 1 ? " package" : " packages");
    if (result != 0)
      report += " (the Newton refused the rest: " + std::to_string(result) + ")";
    return report;
  });
}


void AppDock::beginScreenshots()
{
  Impl & impl = *mImpl;
  unsigned session = mDock->sessionNumber();
  mDock->post([&impl, session](DockController & ioDock) {
    if (ioDock.sessionNumber() != session)
      return;
    impl.screenshots.reset(new ScreenshotSession(ioDock));
    int result = impl.screenshots->begin();
    if (result != 0)
      impl.screenshots.reset();
    if (impl.hooks.screenshotReady)
      impl.hooks.screenshotReady(result);
  });
}


void AppDock::takeScreenshot()
{
  Impl & impl = *mImpl;
  impl.post(*mDock, "Taking a screenshot", [&impl](DockController &, Progress & ioProgress, bool & outOK,
                                                   std::filesystem::path &) {
    Screenshot shot;
    int result = impl.screenshots ? impl.screenshots->take(shot, &ioProgress) : kDockErrFileNotFound;
    outOK = result == 0;
    if (impl.hooks.screenshotTaken)
      impl.hooks.screenshotTaken(shot, result);
    return result == 0 ? "Took a screenshot, " + std::to_string(shot.width) + " x " + std::to_string(shot.height)
                       : "No screenshot (" + std::to_string(result) + ")";
  });
}


void AppDock::endScreenshots()
{
  Impl & impl = *mImpl;
  unsigned session = mDock->sessionNumber();
  mDock->post([&impl, session](DockController & ioDock) {
    if (impl.screenshots && ioDock.sessionNumber() == session)
      impl.screenshots->end();
    impl.screenshots.reset();
  });
}


void AppDock::startKeyboard()
{
  if (mImpl->keyboard)
    mImpl->keyboard->start();
}


void AppDock::typeOnKeyboard(const std::string & inUTF8)
{
  if (mImpl->keyboard)
    mImpl->keyboard->type(inUTF8);
}


void AppDock::pressKey(char16_t inKey)
{
  if (mImpl->keyboard)
    mImpl->keyboard->key(inKey);
}


void AppDock::stopKeyboard()
{
  if (mImpl->keyboard)
    mImpl->keyboard->stop();
}


bool AppDock::keyboardActive() const
{
  return mImpl->keyboard && mImpl->keyboard->active();
}


bool AppDock::postObjectWork(std::function<void()> inWork)
{
  if (!mDock->isRunning())
    return false;
  mDock->postObjectWork(std::move(inWork));
  return true;
}


void AppDock::disconnect()
{
  mDock->disconnect();
}

} // namespace ncx
