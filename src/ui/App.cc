/*
 File: App.cc

 The app (App.h).
 */

#include "ui/App.h"
#include "ui/DocumentWindow.h"
#include "ui/Help.h"
#include "ui/Images.h"
#include "ui/Preferences.h"
#include "ui/RestoreDialog.h"
#include "ui/panels/KeyboardPanel.h"
#include "ui/panels/ScreenshotPanel.h"

#include "base/Core.h"
#include "base/Log.h"
#include "base/Prefs.h"
#include "base/Threads.h"
#include "base/Version.h"
#include "comms/DockProtocol.h"
#include "dock/Packages.h"
#include "platform/AppFolders.h"

#include <FL/Fl.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Sys_Menu_Bar.H>
#include <FL/fl_ask.H>
#if defined(__APPLE__)
#include <FL/platform.H>         // Fl_Mac_App_Menu, fl_open_callback (X11's would clash with the framework's GC)
#endif

#include <atomic>
#include <memory>

namespace ncx {

namespace {

const char * const kBugsURL = "https://github.com/newtonresearch/newton-connection/issues";

std::atomic<bool> gQuitting { false };

/** A question from the dock thread, asked on the UI thread; the dock
    waits for the answer (inDefault if the app quits first). */
bool AskOnMainThread(std::function<bool()> inAsk, bool inDefault)
{
  struct Answer
  {
    Semaphore done;
    bool value = false;
  };
  std::shared_ptr<Answer> answer = std::make_shared<Answer>();
  answer->value = inDefault;
  RunOnMainThread([answer, inAsk]() {
    answer->value = inAsk();
    answer->done.signal();
  });
  while (!answer->done.waitFor(200))
    if (gQuitting)
      return inDefault;
  return answer->value;
}


/** The menus follow the state: checked after each round of events, updated
    when what they depend on changed. */
struct MenuState
{
  DocumentWindow * front = nullptr;
  bool docked = false;
  bool busy = false;
  size_t windows = 0;
  unsigned serial = 0;
  bool operator!=(const MenuState & inOther) const
  {
    return front != inOther.front || docked != inOther.docked || busy != inOther.busy || windows != inOther.windows
        || serial != inOther.serial;
  }
};

MenuState gMenuState;
unsigned gStateSerial = 0;

void CheckMenus(void *)
{
  App & app = App::get();
  MenuState now;
  now.front = app.frontWindow();
  now.docked = app.docked();
  now.busy = app.dock().busy();
  now.windows = app.windows().size();
  now.serial = gStateSerial;
  if (now != gMenuState) {
    gMenuState = now;
    app.updateMenus();
  }
}


#if defined(__APPLE__)
void AboutCB(Fl_Widget *, void *)
{
  App::get().command(Command::kAbout, nullptr);
}

void PreferencesCB(Fl_Widget *, void *)
{
  App::get().command(Command::kPreferences, nullptr);
}

/** macOS: files opened from the Finder, or dropped on the app's icon:
    backups open, packages are installed on the docked Newton. */
void OpenFileCB(const char * inFile)
{
  App & app = App::get();
  std::filesystem::path file = PathFromUTF8(inFile);
  if (IsNewtonPackage(file)) {
    if (app.canDo(Command::kInstallPackage, app.frontWindow()))
      app.installPackages({ file });
    else
      app.alert("To install " + UTF8FromPath(file.filename()) + ", connect a Newton device first.");
    return;
  }
  app.openDocument(file);
}
#endif

} // namespace


App & App::get()
{
  static App app;
  return app;
}


void App::start(const Options & inOptions)
{
#if defined(__APPLE__)
  // one menu bar for the app, there with no window open (D9)
  Fl_Mac_App_Menu::print = "";
  static Fl_Menu_Item appItems[] = {
    { "Preferences" "\xE2\x80\xA6", FL_COMMAND + ',', PreferencesCB, nullptr, 0 },
    { nullptr }
  };
  Fl_Mac_App_Menu::custom_application_menu_items(appItems);
  Fl_Sys_Menu_Bar::about(AboutCB, nullptr);
  Fl_Group::current(nullptr);
  mMenuBar = new Fl_Sys_Menu_Bar(0, 0, 0, 0);
  BuildMenu(*mMenuBar);
  Fl_Sys_Menu_Bar::create_window_menu();
  fl_open_callback(OpenFileCB);
#endif
#if !defined(__APPLE__)
  // the windows' icon (macOS: the bundle's)
  if (Fl_RGB_Image * icon = dynamic_cast<Fl_RGB_Image *>(BuiltinImage("app-icon")))
    Fl_Window::default_icon(icon);
#endif
  for (const std::filesystem::path & file : inOptions.files)
    openDocument(file);
  if (mWindows.empty())
    waitingWindow();
  if (inOptions.dock)
    startDock(inOptions.targets);
  else
    mDockStatus = "Not listening for a Newton";
  Fl::add_check(CheckMenus);
  stateChanged();
}


int App::run()
{
  while (!mQuit && !Fl::program_should_quit()) {
#if !defined(__APPLE__)
    if (!Fl::first_window())
      break;
#endif
    Fl::wait(1e20);
  }
  return 0;
}


void App::stop()
{
  gQuitting = true;
  mDock.stop();
  while (Fl_Window * window = Fl::first_window())
    window->hide();
  Fl::remove_check(CheckMenus);
}


void App::startDock(const std::vector<std::string> & inTargets)
{
  AppDock::Hooks hooks;
  hooks.listening = [this](const std::vector<std::string> & inWhere) {
    std::string text = "Waiting for a Newton";
    for (size_t i = 0; i < inWhere.size(); ++i)
      text += (i == 0 ? " on " : ", ") + inWhere[i];
    RunOnMainThread([this, text]() {
      mDockStatus = text;
      stateChanged();
    });
  };
  hooks.docked = [this](const std::string & inName, const std::filesystem::path & inFile, bool inNewton1,
                        const std::string & inError) {
    RunOnMainThread([this, inName, inFile, inNewton1, inError]() {
      mDocked = true;
      mDockedName = inName;
      mDockedFile = inFile;
      mDockStatus = (inName.empty() ? std::string("A Newton") : inName) + " is docked"
                  + (inNewton1 ? " (NewtonOS 1)" : "");
      if (inFile.empty()) {
        setMessage("Can't keep its backup: " + inError);
      } else {
        // its window: the one open, else the waiting one, else a new one
        DocumentWindow * window = nullptr;
        for (DocumentWindow * each : mWindows)
          if (each->file() == inFile)
            window = each;
        if (!window)
          for (DocumentWindow * each : mWindows)
            if (each->file().empty())
              window = each;
        std::string error;
        if (window && window->file().empty() && !window->bind(inFile, error))
          setMessage(error);
        else if (window)
          window->reload();
        else
          window = openDocument(inFile);
        if (window)
          window->show();
        if (inNewton1)
          newton1Session(window);
        else if (Prefs::App().getBool(pref::kAutoBackup))
          mDock.backUp();
      }
      stateChanged();
    });
  };
  hooks.undocked = [this](int inCode, const std::string & inWhy) {
    RunOnMainThread([this, inCode, inWhy]() {
      mDocked = false;
      mDockedFile.clear();
      mDockedName.clear();
      if (inCode != 0)
        alert("The connection ended: " + inWhy);
      mDockStatus = "Waiting for a Newton";
      if (mRestartDock) {
        mRestartDock = false;
        preferencesChanged();     // the dock listens where the preferences say now
      }
      stateChanged();
    });
  };
  hooks.status = [this](const std::string & inText) {
    RunOnMainThread([this, inText]() {
      if (!inText.empty())
        setMessage(inText);
      stateChanged();
    });
  };
  hooks.started = [this](const std::string & inTitle) {
    RunOnMainThread([this, inTitle]() {
      setMessage(inTitle);
      stateChanged();
    });
  };
  hooks.finished = [this](const std::string & inReport, bool inOK, const std::filesystem::path & inFile) {
    RunOnMainThread([this, inReport, inOK, inFile]() {
      setMessage(inReport);
      if (!inFile.empty())
        fileChanged(inFile);
      // a failure (not a cancel) is told
      if (!inOK && inReport.find("(cancelled)") == std::string::npos)
        alert(inReport);
      stateChanged();
    });
  };
  hooks.confirmDeletions = [](const std::string & inText) {
    return AskOnMainThread([inText]() {
      fl_message_title("Back Up");
      return fl_choice("%s", "Only Add", "Delete", nullptr, inText.c_str()) == 1;
    }, false);
  };
  hooks.replacePackage = [](const std::string & inName) {
    return AskOnMainThread([inName]() {
      fl_message_title("Install Package");
      return fl_choice("The package \"%s\" is on the Newton already. Replace it?", "Keep", "Replace", nullptr,
                       inName.c_str()) == 1;
    }, false);
  };
  hooks.screenshotReady = [this](int inError) {
    RunOnMainThread([this, inError]() {
      for (DocumentWindow * window : mWindows)
        if (isDockedWindow(window))
          window->screenshotPanel()->ready(inError);
    });
  };
  hooks.screenshotTaken = [this](const Screenshot & inShot, int inError) {
    RunOnMainThread([this, inShot, inError]() {
      for (DocumentWindow * window : mWindows)
        if (isDockedWindow(window))
          window->screenshotPanel()->taken(inShot, inError);
    });
  };
  hooks.keyboardChanged = [this](bool inOn) {
    RunOnMainThread([this, inOn]() {
      for (DocumentWindow * window : mWindows)
        if (isDockedWindow(window)) {
          if (inOn && !window->keyboardPanel()->active())
            window->showKeyboard();       // the Newton started it
          window->keyboardPanel()->changed(inOn);
        }
      stateChanged();
    });
  };
  mHooks = hooks;
  mTargets = inTargets;
  mListening = true;
  std::string error;
  mDockStatus = "Waiting for a Newton";
  if (!mDock.start(hooks, error, inTargets))
    mDockStatus = "Can't wait for a Newton: " + error;
}


void App::newton1Session(DocumentWindow * inWindow)
{
  // NewtonOS 1 docks for one thing, chosen before (the Newton 1 menu)
  switch (Prefs::App().getInt(pref::kNewton1SessionType, kSynchronizeSession)) {
    case kRestoreSession:
      command(Command::kRestore, inWindow);
      break;
    case kLoadPackageSession:
      chooseAndInstall();
      break;
    default:
      mDock.backUp();
      break;
  }
}


void App::preferencesChanged()
{
  // logging: at once
  SetLogFile(std::filesystem::path());
  StartLogging();
  // where to listen (only the preferences', not targets given)
  if (!mListening || !mTargets.empty())
    return;
  if (mDocked) {
    mRestartDock = true;
    return;
  }
  mDock.stop();
  std::string error;
  mDockStatus = "Waiting for a Newton";
  if (!mDock.start(mHooks, error))
    mDockStatus = "Can't wait for a Newton: " + error;
  stateChanged();
}


void App::alert(const std::string & inText)
{
  setMessage(inText);
  Log(kLogMinimum, "ui: %s", inText.c_str());
  stateChanged();
  if (mAlert)
    mAlert(inText);
  else {
    fl_message_title(NCX_NAME);
    fl_alert("%s", inText.c_str());
  }
}


void App::setMessage(const std::string & inText, const std::filesystem::path & inFile)
{
  mMessage = inText;
  mMessageFile = inFile;
}


void App::fileChanged(const std::filesystem::path & inFile)
{
  for (DocumentWindow * window : mWindows)
    if (window->file() == inFile)
      window->reload();
}


void App::selectionChanged()
{
  ++gStateSerial;
}


void App::stateChanged()
{
  ++gStateSerial;
  for (DocumentWindow * window : mWindows)
    window->updateState();
}


/*-- commands -----------------------------------------------------------------*/

bool App::isDockedWindow(const DocumentWindow * inWindow) const
{
  return inWindow && mDocked && !mDockedFile.empty() && inWindow->file() == mDockedFile;
}


bool App::canDo(Command inCommand, DocumentWindow * inTarget) const
{
  bool idle = mDocked && !mDockedFile.empty() && !mDock.busy();
  switch (inCommand) {
    case Command::kAbout:
    case Command::kPreferences:
    case Command::kQuit:
    case Command::kReportBug:
    case Command::kHelp:
    case Command::kNewConnection:
    case Command::kOpen:
      return true;
    case Command::kClose:
    case Command::kToggleInspector:
      return inTarget != nullptr;
    case Command::kToggleSidebar:
      return inTarget && !inTarget->file().empty();
    case Command::kBackUp:
    case Command::kRestore:
      return idle && isDockedWindow(inTarget);
    case Command::kInstallPackage:
      return idle;
    case Command::kDumpROM:
    case Command::kDumpCard:
      return idle && !mDock.newton1() && isDockedWindow(inTarget);
    case Command::kDisconnect:
      return mDocked;
    case Command::kDelete:
      return inTarget && inTarget->canDeleteEntries();
    case Command::kExport:
      return inTarget && inTarget->canExport();
    case Command::kSelectAll:
      return inTarget && inTarget->canSelectAll();
    case Command::kPaste:
      return inTarget && inTarget->canPaste();
    case Command::kImport:
      return inTarget && inTarget->canImport() && (!isDockedWindow(inTarget) || !mDock.busy());
    case Command::kNewton1Sync:
    case Command::kNewton1Restore:
    case Command::kNewton1Install:
      return !mDocked;
    default:
      return false;     // to come (PORTING.md 7.4 ...)
  }
}


bool App::isChecked(Command inCommand, DocumentWindow *) const
{
  int session = Prefs::App().getInt(pref::kNewton1SessionType, kSynchronizeSession);
  switch (inCommand) {
    case Command::kNewton1Sync:     return session != kRestoreSession && session != kLoadPackageSession;
    case Command::kNewton1Restore:  return session == kRestoreSession;
    case Command::kNewton1Install:  return session == kLoadPackageSession;
    default:                        return false;
  }
}


void App::command(Command inCommand, DocumentWindow * inTarget)
{
  if (!canDo(inCommand, inTarget))
    return;
  switch (inCommand) {
    case Command::kAbout:
      fl_message_title("About " NCX_NAME);
      fl_message("%s %s\n\nThe Newton's desktop companion,\nfor Windows, Linux and macOS.", NCX_NAME, NCX_VERSION);
      break;
    case Command::kPreferences:
      PreferencesWindow::Show();
      break;
    case Command::kQuit:
      mQuit = true;
      break;
    case Command::kReportBug:
      fl_open_uri(kBugsURL);
      break;
    case Command::kHelp:
      HelpWindow::Show();
      break;
    case Command::kNewConnection:
      waitingWindow()->show();
      break;
    case Command::kOpen:
      chooseAndOpen();
      break;
    case Command::kClose:
      inTarget->hide();
      break;
    case Command::kToggleSidebar:
      inTarget->showSidebar(!inTarget->sidebarShown());
      break;
    case Command::kToggleInspector:
      inTarget->showInspector(!inTarget->inspectorShown());
      break;
    case Command::kBackUp:
      mDock.backUp();
      break;
    case Command::kRestore: {
      // what can be restored (the dock asks the Newton), then what to restore
      std::filesystem::path file = mDockedFile;
      setMessage("Restore: reading what can be restored...");
      mDock.restoreChoices(file, [this, file](const std::vector<RestoreChoice> & inChoices, const std::string & inError) {
        RunOnMainThread([this, file, inChoices, inError]() {
          if (!inError.empty() || !mDocked || mDockedFile != file) {
            setMessage("Nothing restored: " + (inError.empty() ? std::string("the Newton disconnected") : inError));
            stateChanged();
            return;
          }
          std::optional<RestoreSelection> selection;
          if (mRestoreChooser)
            selection = mRestoreChooser(inChoices);
          else {
            RestoreDialog dialog(inChoices);
            selection = dialog.run();
          }
          if (selection && mDocked && mDockedFile == file)
            mDock.restore(file, *selection);
          else
            setMessage("Nothing restored");
          stateChanged();
        });
      });
      break;
    }
    case Command::kInstallPackage:
      chooseAndInstall();
      break;
    case Command::kDumpROM: {
      Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
      chooser.title("Save the Newton's ROM");
      std::string name = (mDockedName.empty() ? std::string("Newton") : mDockedName) + " ROM.bin";
      chooser.preset_file(name.c_str());
      if (chooser.show() != 0 || !chooser.filename())
        break;
      std::filesystem::path file = PathFromUTF8(chooser.filename());
      bool resume = false;
      std::error_code error;
      if (std::filesystem::exists(file, error)) {
        fl_message_title("Dump ROM");
        int answer = fl_choice("%s is there already. Go on where it stopped, or start again?", "Cancel", "Resume",
                               "Start Again", UTF8FromPath(file.filename()).c_str());
        if (answer == 0)
          break;
        resume = answer == 1;
      }
      mDock.dumpROM(file, resume);
      break;
    }
    case Command::kDumpCard: {
      Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_DIRECTORY);
      chooser.title("Save the cards into this folder");
      if (chooser.show() == 0 && chooser.filename())
        mDock.dumpCards(PathFromUTF8(chooser.filename()));
      break;
    }
    case Command::kDisconnect:
      mDock.disconnect();
      break;
    case Command::kDelete:
      inTarget->deleteEntries();
      break;
    case Command::kExport:
      inTarget->exportEntries();
      break;
    case Command::kSelectAll:
      inTarget->selectAll();
      break;
    case Command::kPaste:
      inTarget->paste();
      break;
    case Command::kImport: {
      Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_MULTI_FILE);
      chooser.title("Import");
      if (chooser.show() == 0) {
        std::vector<std::filesystem::path> files;
        for (int i = 0; i < chooser.count(); ++i)
          files.push_back(PathFromUTF8(chooser.filename(i)));
        inTarget->importFiles(files);
      }
      break;
    }
    // what the app does when a NewtonOS 1 Newton docks (the Cocoa app's
    // Newton 1 menu: its tags, the session types)
    case Command::kNewton1Sync:
      Prefs::App().setInt(pref::kNewton1SessionType, kSynchronizeSession);
      break;
    case Command::kNewton1Restore:
      Prefs::App().setInt(pref::kNewton1SessionType, kRestoreSession);
      break;
    case Command::kNewton1Install:
      Prefs::App().setInt(pref::kNewton1SessionType, kLoadPackageSession);
      break;
    default:
      break;
  }
  stateChanged();
}


void App::updateMenus()
{
  if (mMenuBar)
    UpdateMenu(*mMenuBar);
  for (DocumentWindow * window : mWindows)
    if (Fl_Menu_ * menu = window->menuBar())
      UpdateMenu(*menu);
}


void App::chooseAndOpen()
{
  Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_FILE);
  chooser.title("Open a Newton backup");
  chooser.filter("Newton backups\t*.newtondevice");
  std::string folder = UTF8FromPath(AppDataFolder());
  chooser.directory(folder.c_str());
  if (chooser.show() == 0 && chooser.filename())
    openDocument(PathFromUTF8(chooser.filename()));
}


void App::chooseAndInstall()
{
  Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_MULTI_FILE);
  chooser.title("Install packages");
  chooser.filter("Newton packages\t*.{newtonpkg,pkg,PKG}");
  if (chooser.show() != 0)
    return;
  std::vector<std::filesystem::path> files;
  for (int i = 0; i < chooser.count(); ++i)
    files.push_back(PathFromUTF8(chooser.filename(i)));
  installPackages(files);
}


void App::objectWork(std::function<void()> inWork)
{
  if (!mDock.postObjectWork(inWork))
    inWork();
}


void App::installPackages(const std::vector<std::filesystem::path> & inFiles)
{
  if (inFiles.empty() || !canDo(Command::kInstallPackage, frontWindow()))
    return;
  mDock.install(inFiles);
  stateChanged();
}


/*-- windows ------------------------------------------------------------------*/

DocumentWindow * App::frontWindow() const
{
  for (Fl_Window * window = Fl::first_window(); window; window = Fl::next_window(window))
    if (DocumentWindow * document = dynamic_cast<DocumentWindow *>(window))
      return document;
  return nullptr;
}


DocumentWindow * App::newWindow()
{
  Fl_Group::current(nullptr);
  DocumentWindow * window = new DocumentWindow();
  mWindows.push_back(window);
  return window;
}


DocumentWindow * App::openDocument(const std::filesystem::path & inFile)
{
  std::error_code ignored;
  std::filesystem::path file = std::filesystem::absolute(inFile, ignored);
  for (DocumentWindow * window : mWindows)
    if (window->file() == file) {
      window->show();
      return window;
    }
  DocumentWindow * window = newWindow();
  std::string error;
  if (!window->bind(file, error)) {
    mWindows.pop_back();
    Fl::delete_widget(window);
    fl_message_title("Open");
    fl_alert("%s", error.c_str());
    return nullptr;
  }
  window->show();
  stateChanged();
  return window;
}


DocumentWindow * App::waitingWindow()
{
  for (DocumentWindow * window : mWindows)
    if (window->file().empty())
      return window;
  DocumentWindow * window = newWindow();
  window->show();
  stateChanged();
  return window;
}


void App::windowClosed(DocumentWindow * inWindow)
{
  bool waiting = inWindow->file().empty();
  for (auto it = mWindows.begin(); it != mWindows.end(); ++it)
    if (*it == inWindow) {
      mWindows.erase(it);
      break;
    }
  Fl::delete_widget(inWindow);
#if !defined(__APPLE__)
  // the last document closed: the chooser again (closing that one quits;
  // macOS: the app stays, with its menu bar)
  if (mWindows.empty() && !waiting && !mQuit && !gQuitting)
    waitingWindow();
#else
  (void)waiting;
#endif
  stateChanged();
}

} // namespace ncx
