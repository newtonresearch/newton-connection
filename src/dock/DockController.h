/*
 File: DockController.h

 The dock (PORTING.md 5.1; the Cocoa app's NCDockProtocolController): a
 thread of its own listens for a Newton, docks it (Handshake.h), and then
 serves the session until it ends:
   - operations the desktop asks for (post(): a backup, a screenshot, ...)
     run in order, one at a time;
   - requests the Newton makes on its own (browsing for a package, a
     cancel, 'disc', ...) go to the handlers the components registered.
 Then it listens again, or ends (Settings::once, for ncx-cli).

 Only the dock thread touches Newton objects (Refs) and the session: an
 operation gets the controller, does its work there, and hands plain C++
 results to the UI (RunOnMainThread). The Events are called on the dock
 thread too.
 */

#ifndef NCX_DOCK_DOCKCONTROLLER_H
#define NCX_DOCK_DOCKCONTROLLER_H

#include "comms/Handshake.h"
#include "comms/Session.h"

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace ncx {

class EventQueue;
class Listener;

class DockController
{
public:
  struct Settings
  {
    std::vector<std::string> targets;     // Listener.h
    std::string password;                 // the Newton's
    DockAppInfo app;
    bool once = false;                    // end after one session (ncx-cli)
    int waitSeconds = -1;                 // give up waiting for a Newton then (-1: never)
    bool keepTrying = false;              // the app: if no target can be used, the thread runs
                                          // anyway (object work) and tries again every few seconds
  };

  /** What happens, told on the dock thread. */
  struct Events
  {
    std::function<void(const std::vector<std::string> &)> listening;   // where
    std::function<void(const DockedNewton &)> docked;
    std::function<void(int, const std::string &)> undocked;           // kDockErr..., why
    std::function<void(const std::string &)> status;                  // "" when done
  };

  /** An operation from the desktop. A DockFailure it throws ends the
      session. */
  typedef std::function<void(DockController &)> Task;
  /** A request the Newton makes on its own. */
  typedef std::function<void(DockController &, const DockEvent &)> Handler;

  DockController();
  ~DockController();
  DockController(const DockController &) = delete;
  DockController & operator=(const DockController &) = delete;

  void setEvents(const Events & inEvents) { mEvents = inEvents; }
  /** A handler for a request from the Newton (before start()). */
  void handle(EventType inCommand, Handler inHandler) { mHandlers[inCommand] = inHandler; }

  /** Start listening (the dock thread). False if no target can be used. */
  bool start(const Settings & inSettings, std::string & outError);
  /** Disconnect if docked, stop listening, end the dock thread. */
  void stop();
  /** Until the dock thread ends (with once: after the session). */
  void wait();
  bool isDocked() const { return mDocked; }
  /** Counts the sessions: 1 for the first, ... (0 before any). */
  unsigned sessionNumber() const { return mSessionNumber; }
  /** How the last session ended: 0 if the desktop or the Newton ended it
      as they should, else the DockFailure's code (and why()). */
  int result() const { return mResult; }
  std::string why() const;

  /** An operation, run on the dock thread once docked, after the ones
      before it. Operations posted while not docked wait for the next
      session; stop() drops them. */
  void post(Task inTask);
  /** End the session: 'disc' to the Newton (posted, after the operations
      before it). */
  void disconnect();

  /** Work with Newton objects that needs no Newton (exporting, an entry's
      text for the UI): run on the dock thread soon, docked or not (between
      operations), in order. It must not use the session. Dropped by
      stop(). */
  typedef std::function<void()> ObjectWork;
  void postObjectWork(ObjectWork inWork);
  /** The dock thread runs (start() made it, stop() hasn't ended it). */
  bool isRunning() const { return mThread.joinable() && !mStopping; }

  /*-- the dock thread only --*/

  Session & session() { return *mSession; }
  const DockedNewton & newton() const { return mNewton; }
  /** kDDesktopInControl (true) or kDOperationDone (false); the latter
      also lets the session time out again (screenshot, keyboard).
      Nothing for NewtonOS 1 (it has no desktop control). The
      kDOperationDone waits until no operation follows: a 'disc' takes its
      place (the Newton then ends the session quietly), and control asked
      for again right away needs neither. */
  void setDesktopControl(bool inControl);
  /** The desktop is in control (setDesktopControl(true), not given up). */
  bool inControl() const { return mInControl; }
  /** An extension (Extensions.h) to the Newton, once a session. */
  void loadExtension(EventType inId);
  void setStatus(const std::string & inText);

private:
  void run();
  bool listen(std::string & outError);
  void setListener(std::unique_ptr<Listener> inListener);
  void serve();
  void handleNewton(const DockEvent & inEvent);
  bool runTasks();
  void runObjectWork();
  bool hasObjectWork();
  void endSession(int inCode, const std::string & inWhy);
  void flushOperationDone();
  void sendDisconnect();

  Settings mSettings;
  Events mEvents;
  std::map<EventType, Handler> mHandlers;
  std::thread mThread;
  std::mutex mListenerLock;                 // mListener: replaced by the dock thread, stopped by stop()
  std::unique_ptr<Listener> mListener;
  std::unique_ptr<EventQueue> mQueue;
  std::unique_ptr<Session> mSession;
  DockedNewton mNewton;
  std::set<EventType> mExtensions;          // loaded this session
  bool mOperationDonePending = false;       // kDOperationDone due (setDesktopControl)
  bool mInControl = false;
  std::mutex mLock;                         // mTasks, mWhy
  std::deque<Task> mTasks;
  std::deque<ObjectWork> mObjectWork;
  std::string mWhy;
  std::atomic<bool> mStopping { false };
  std::atomic<bool> mDocked { false };
  std::atomic<unsigned> mSessionNumber { 0 };
  std::atomic<int> mResult { 0 };
};

} // namespace ncx

#endif // NCX_DOCK_DOCKCONTROLLER_H
