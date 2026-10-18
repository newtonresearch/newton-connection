/*
 File: AppDock.h

 The app's dock (PORTING.md 7.2): the DockController, with what the app
 does with a Newton:
   - it listens all the time, for one Newton after the other (keepTrying:
     also when no port can be opened yet), where the preferences say
     (DefaultTargets)
   - a Newton that docks gets its .newtondevice file in the app's folder
     (found by its id, or made) brought up to date (UpdateDevice): then
     the UI hears of it
   - the Newton may browse the desktop (Dock's Install) and back itself up
     (Dock's Backup); the desktop may back it up and install packages
 One operation at a time has a Progress the UI shows and may cancel.

 The Hooks are called on the dock thread: the App takes them to the UI
 thread (RunOnMainThread), but for the questions, which wait for the
 answer. No FLTK and no newton-framework headers here (they clash): the
 dock's headers stay in AppDock.cc.
 */

#ifndef NCX_UI_APPDOCK_H
#define NCX_UI_APPDOCK_H

#include "base/Progress.h"
#include "dock/Screenshot.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ncx {

class DockController;

class AppDock
{
public:
  struct Hooks
  {
    std::function<void(const std::vector<std::string> &)> listening;    // where
    /** Docked, and its file up to date (inFile; "" if it couldn't be
        opened: inError). */
    std::function<void(const std::string & inName, const std::filesystem::path & inFile, bool inNewton1,
                       const std::string & inError)> docked;
    std::function<void(int inCode, const std::string & inWhy)> undocked;  // 0: as it should
    std::function<void(const std::string &)> status;                     // "" when done
    /** An operation started (its title: "Backing up") ... */
    std::function<void(const std::string & inTitle)> started;
    /** ... and ended: what came of it ("12 soups backed up, ..."),
        whether all went well, the file it changed ("" none). */
    std::function<void(const std::string & inReport, bool inOK, const std::filesystem::path & inFile)> finished;
    /** Questions; the dock waits for the answer. Delete the entries the
        text names on the Newton? */
    std::function<bool(const std::string & inText)> confirmDeletions;
    /** Replace the package of that name on the Newton? */
    std::function<bool(const std::string & inName)> replacePackage;
    /** Screenshots: the Newton is ready (0), or why not
        (kDockErrFileNotFound: no Toolkit app); a shot taken (0), or why
        not. */
    std::function<void(int inError)> screenshotReady;
    std::function<void(const Screenshot & inShot, int inError)> screenshotTaken;
    /** Keyboard passthrough on (the Newton answered, or started it: Dock's
        Keyboard) or off (ended, on the Newton or here). */
    std::function<void(bool inOn)> keyboardChanged;
  };

  AppDock();
  ~AppDock();
  AppDock(const AppDock &) = delete;
  AppDock & operator=(const AppDock &) = delete;

  /** Listen (inTargets: none, the preferences'). False and why if the dock
      thread can't run; it may run and still not listen yet (keepTrying). */
  bool start(const Hooks & inHooks, std::string & outError, const std::vector<std::string> & inTargets = {});
  /** Disconnect, end the dock thread. */
  void stop();
  /** Where the app listens by the preferences: tcp-all:<TCPIPPort>, the
      serial port at its speed (SerialPort, BaudRate) if one is set, and on
      macOS Einstein's pipes. */
  static std::vector<std::string> DefaultTargets();

  bool docked() const;
  /** The docked Newton runs NewtonOS 1. */
  bool newton1() const;
  /** An operation is in progress (or posted). */
  bool busy() const;
  /** The operation's progress (nullptr: none). */
  std::shared_ptr<Progress> progress() const;
  void cancel();

  /** Operations, posted (they run once docked, one after the other). */
  void backUp();
  void install(const std::vector<std::filesystem::path> & inFiles);
  /** Delete entries of a soup on the Newton and from its file (packages:
      removed by name). */
  void removeEntries(const std::filesystem::path & inFile, int64_t inSoupPk, const std::vector<int64_t> & inEntryPks);
  void disconnect();

  /** Screenshots (ScreenshotSession): the Newton gets ready (the Dock
      slip goes away), shots are taken (operations), the slip comes back.
      Told through the hooks. */
  void beginScreenshots();
  void takeScreenshot();
  void endScreenshots();

  /** Keyboard passthrough (Keyboard.h): any thread. */
  void startKeyboard();
  void typeOnKeyboard(const std::string & inUTF8);
  void pressKey(char16_t inKey);
  void stopKeyboard();
  bool keyboardActive() const;

  /** Work with Newton objects on the dock thread (DockController::
      postObjectWork). False if the dock thread doesn't run. */
  bool postObjectWork(std::function<void()> inWork);

  DockController & controller() { return *mDock; }

private:
  struct Impl;
  std::unique_ptr<Impl> mImpl;
  std::unique_ptr<DockController> mDock;
};

} // namespace ncx

#endif // NCX_UI_APPDOCK_H
