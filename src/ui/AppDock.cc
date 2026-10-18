/*
 File: AppDock.cc

 The app's dock (AppDock.h).
 */

#include "ui/AppDock.h"

#include "base/Log.h"
#include "base/Prefs.h"
#include "dock/Backup.h"
#include "dock/DockController.h"
#include "dock/FileBrowser.h"
#include "dock/Packages.h"
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
  settings.targets = inTargets;
  if (settings.targets.empty()) {
    settings.targets.push_back("tcp-all:" + std::to_string(Prefs::App().tcpPort()));
#if defined(__APPLE__)
    settings.targets.push_back("einstein-pipes");
#endif
  }
  settings.password = Prefs::App().getString(pref::kPassword);
  settings.keepTrying = true;

  // the Newton browses the desktop (Dock's Install), and backs itself up
  // (Dock's Backup)
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


void AppDock::disconnect()
{
  mDock->disconnect();
}

} // namespace ncx
