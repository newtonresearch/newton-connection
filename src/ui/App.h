/*
 File: App.h

 The app (PORTING.md 7.2, 7.3): its document windows (one per
 .newtondevice file, D9; and one "Waiting for a Newton" while none is
 docked), its dock (AppDock.h), and its commands (Commands.h): which can be
 done now, and doing them.

 A Newton that docks gets the window of its file: the one open, else the
 waiting one, else a new one. Off macOS, closing the last window quits
 (the device chooser comes with 7.7); on macOS the app stays, with its
 menu bar.

 The UI thread only, but for what the dock tells it (taken to the UI
 thread here).
 */

#ifndef NCX_UI_APP_H
#define NCX_UI_APP_H

#include "ui/AppDock.h"
#include "ui/Commands.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class Fl_Sys_Menu_Bar;

namespace ncx {

class DocumentWindow;

class App
{
public:
  static App & get();

  struct Options
  {
    bool dock = true;                             // listen for Newtons
    std::vector<std::filesystem::path> files;     // to open
    std::vector<std::string> targets;             // where to listen (Listener.h; none: the preferences')
  };

  /** The windows, the menu bar (macOS), the dock. */
  void start(const Options & inOptions);
  /** Until quit (or no window is left, off macOS). */
  int run();
  /** The dock ends; windows close. */
  void stop();

  /*-- commands --*/

  void command(Command inCommand, DocumentWindow * inTarget);
  bool canDo(Command inCommand, DocumentWindow * inTarget) const;
  bool isChecked(Command inCommand, DocumentWindow * inTarget) const;
  /** The menus as the state is now (after it changed). */
  void updateMenus();

  /*-- windows --*/

  /** The topmost document window (nullptr: none). */
  DocumentWindow * frontWindow() const;
  /** The window of a file: the one open, else a new one; shown. nullptr
      (and an alert) if the file can't be opened. */
  DocumentWindow * openDocument(const std::filesystem::path & inFile);
  /** A window that waits for a Newton (the one there, else a new one). */
  DocumentWindow * waitingWindow();
  /** A window was closed (it deletes itself later). */
  void windowClosed(DocumentWindow * inWindow);
  const std::vector<DocumentWindow *> & windows() const { return mWindows; }

  /*-- the dock, as windows show it --*/

  bool docked() const { return mDocked; }
  /** The docked Newton's file ("" none, or not up to date yet). */
  const std::filesystem::path & dockedFile() const { return mDockedFile; }
  const std::string & dockedName() const { return mDockedName; }
  /** "Waiting for a Newton on ...", "Matt's MessagePad is docked", ... */
  const std::string & dockStatus() const { return mDockStatus; }
  /** The last operation's report, or what the dock says now. */
  const std::string & message() const { return mMessage; }
  AppDock & dock() { return mDock; }
  /** The window shows the docked Newton's file. */
  bool isDockedWindow(const DocumentWindow * inWindow) const;

private:
  App() { }
  void startDock(const std::vector<std::string> & inTargets);
  void stateChanged();
  DocumentWindow * newWindow();
  void chooseAndOpen();
  void chooseAndInstall();

  AppDock mDock;
  std::vector<DocumentWindow *> mWindows;
  Fl_Sys_Menu_Bar * mMenuBar = nullptr;       // macOS
  bool mDocked = false;
  bool mQuit = false;
  std::filesystem::path mDockedFile;
  std::string mDockedName;
  std::string mDockStatus;
  std::string mMessage;
};

} // namespace ncx

#endif // NCX_UI_APP_H
