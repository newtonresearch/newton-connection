/*
 File: DockController.cc

 The dock (DockController.h).
 */

#include "dock/DockController.h"
#include "dock/Extensions.h"
#include "base/Core.h"
#include "base/Log.h"
#include "base/NewtonCall.h"
#include "comms/EventQueue.h"
#include "comms/Listener.h"

#include <chrono>

namespace ncx {

namespace {

const int kIdleWait = 50;            // ms between looks at the operations while idle

} // namespace


DockController::DockController()
{
}


DockController::~DockController()
{
  stop();
}


bool DockController::start(const Settings & inSettings, std::string & outError)
{
  stop();
  mSettings = inSettings;
  mStopping = false;
  mResult = 0;
  {
    std::lock_guard<std::mutex> lock(mLock);
    mWhy.clear();
  }
  // the first listener here, so that a target that can't be used is told
  bool listening = listen(outError);
  if (!listening && !mSettings.keepTrying)
    return false;
  mThread = std::thread([this] { run(); });
  return listening;
}


void DockController::stop()
{
  mStopping = true;
  {
    std::lock_guard<std::mutex> lock(mListenerLock);
    if (mListener)
      mListener->stop();
  }
  if (mThread.joinable())
    mThread.join();
  std::lock_guard<std::mutex> lock(mLock);
  mTasks.clear();
  mObjectWork.clear();
}


void DockController::wait()
{
  if (mThread.joinable())
    mThread.join();
}


std::string DockController::why() const
{
  std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(mLock));
  return mWhy;
}


void DockController::post(Task inTask)
{
  std::lock_guard<std::mutex> lock(mLock);
  mTasks.push_back(std::move(inTask));
}


void DockController::postObjectWork(ObjectWork inWork)
{
  std::lock_guard<std::mutex> lock(mLock);
  mObjectWork.push_back(std::move(inWork));
}


bool DockController::hasObjectWork()
{
  std::lock_guard<std::mutex> lock(mLock);
  return !mObjectWork.empty();
}


void DockController::runObjectWork()
{
  for (;;) {
    ObjectWork work;
    {
      std::lock_guard<std::mutex> lock(mLock);
      if (mObjectWork.empty())
        return;
      work = std::move(mObjectWork.front());
      mObjectWork.pop_front();
    }
    try {
      CallNewton([&] { work(); });
    } catch (const std::exception & e) {
      Log(kLogMinimum, "dock: %s in object work", e.what());
    }
  }
}


void DockController::disconnect()
{
  post([](DockController & dock) {
    dock.sendDisconnect();
    dock.endSession(0, "");
  });
}


/*-- the dock thread ---------------------------------------------------------*/

bool DockController::listen(std::string & outError)
{
  std::unique_ptr<Listener> listener(new Listener());
  if (!listener->start(mSettings.targets, outError))
    return false;
  if (mEvents.listening)
    mEvents.listening(listener->listening());
  setListener(std::move(listener));
  return true;
}


void DockController::setListener(std::unique_ptr<Listener> inListener)
{
  std::lock_guard<std::mutex> lock(mListenerLock);
  mListener = std::move(inListener);
  if (mListener && mStopping)
    mListener->stop();
}


void DockController::run()
{
  InitCore();
  bool haveListener;
  {
    std::lock_guard<std::mutex> lock(mListenerLock);
    haveListener = mListener != nullptr;
  }
  for (bool first = true; !mStopping; first = false) {
    std::string error;
    if ((!first || !haveListener) && !listen(error)) {
      if (!mSettings.keepTrying) {
        endSession(kDockErrBadConnection, error);
        break;
      }
      // no target can be used (yet): object work, and again in a while
      Log(kLogMinimum, "dock: can't listen: %s", error.c_str());
      for (int i = 0; i < 50 && !mStopping; ++i) {
        runObjectWork();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      continue;
    }
    // wait for a Newton (object work in between)
    std::unique_ptr<Connection> connection;
    auto started = std::chrono::steady_clock::now();
    while (!mStopping && !connection) {
      runObjectWork();
      connection = mListener->waitForConnection(hasObjectWork() ? 0 : 100, error);
      if (!connection && !error.empty()) {
        Log(kLogMinimum, "dock: %s", error.c_str());
        error.clear();
      }
      if (!connection && mSettings.waitSeconds >= 0
          && std::chrono::steady_clock::now() - started > std::chrono::seconds(mSettings.waitSeconds)) {
        endSession(kDockErrBadConnection, "no Newton came");
        setListener(nullptr);
        return;
      }
    }
    setListener(nullptr);
    if (!connection)
      break;
    Log(kLogMinimum, "dock: connected, %s", connection->describe().c_str());

    // dock it
    mQueue.reset(new EventQueue(std::move(connection)));
    mSession.reset(new Session(*mQueue));
    mExtensions.clear();
    mOperationDonePending = false;
    mInControl = false;
    bool docked = false;        // (endSession() clears mDocked)
    try {
      CallNewton([this] { mNewton = Handshake(*mSession, mSettings.password, mSettings.app, 60); });
      ++mSessionNumber;
      mDocked = true;
      docked = true;
      mResult = 0;
      if (mEvents.docked)
        mEvents.docked(mNewton);
      serve();
    } catch (const DockFailure & failure) {
      endSession(failure.code, failure.what());
    } catch (const std::exception & e) {
      endSession(kDockErrDesktopError, e.what());
    }
    mSession.reset();
    if (mQueue)
      mQueue->close();
    mQueue.reset();
    mDocked = false;
    if (docked && mEvents.undocked)
      mEvents.undocked(mResult, why());
    if (mSettings.once)
      break;
  }
  setListener(nullptr);
}


void DockController::serve()
{
  mSession->startTickler();
  while (!mStopping && mDocked) {
    if (!runTasks())
      break;
    runObjectWork();
    DockEvent event;
    EventQueue::Result result = mQueue->next(event, kIdleWait);
    if (result == EventQueue::kClosed) {
      endSession(kDockErrDisconnected, mQueue->why());
      break;
    }
    if (result == EventQueue::kEvent)
      handleNewton(event);
  }
  if (mStopping && mDocked) {
    // stop(): end the session as the desktop should
    try {
      sendDisconnect();
    } catch (const DockFailure &) {
    }
    endSession(0, "");
  }
  mSession->stopTickler();
}


bool DockController::runTasks()
{
  while (mDocked && !mStopping) {
    Task task;
    {
      std::lock_guard<std::mutex> lock(mLock);
      if (!mTasks.empty()) {
        task = std::move(mTasks.front());
        mTasks.pop_front();
      }
    }
    if (!task) {
      // nothing more to do: the end of desktop control, if one is due
      try {
        flushOperationDone();
      } catch (const DockFailure & failure) {
        endSession(failure.code, failure.what());
        return false;
      }
      return true;
    }
    try {
      CallNewton([&] { task(*this); });
    } catch (const DockFailure & failure) {
      endSession(failure.code, failure.what());
      return false;
    } catch (const NewtonException & e) {
      // the operation failed, the session goes on (as the Cocoa app's
      // newton_catch_all around each operation)
      Log(kLogMinimum, "dock: %s in an operation", e.what());
      setStatus(e.what());
    }
  }
  return mDocked;
}


void DockController::handleNewton(const DockEvent & inEvent)
{
  EventType command = inEvent.command;
  if (command == kDHello)
    return;
  if (command == kDDisconnect) {
    // the Newton ends the session
    endSession(0, "");
    return;
  }
  auto handler = mHandlers.find(command);
  if (handler != mHandlers.end()) {
    try {
      CallNewton([&] { handler->second(*this, inEvent); });
    } catch (const DockFailure & failure) {
      endSession(failure.code, failure.what());
    } catch (const NewtonException & e) {
      Log(kLogMinimum, "dock: %s in '%s'", e.what(), CmdName(command).c_str());
      setStatus(e.what());
    }
    return;
  }
  if (command == kDOperationCanceled) {
    // nothing going on to cancel: acknowledge it (the Newton waits for that)
    mSession->send(kDOpCanceledAck);
    return;
  }
  Log(kLogMinimum, "dock: '%s' from the Newton isn't handled", CmdName(command).c_str());
  mSession->sendValue(kDUnknownCommand, (int32_t)command);
}


void DockController::endSession(int inCode, const std::string & inWhy)
{
  mResult = inCode;
  {
    std::lock_guard<std::mutex> lock(mLock);
    mWhy = inWhy;
  }
  if (inCode != 0)
    Log(kLogMinimum, "dock: the session ended: %s (%d)", inWhy.c_str(), inCode);
  // ended as it should ('disc', sent or received): the Newton drops the
  // connection itself
  if (mQueue)
    mQueue->close(inCode == 0);
  mDocked = false;
}


void DockController::setDesktopControl(bool inControl)
{
  if (mNewton.newton1) {
    // NewtonOS 1 has no desktop control: the desktop asks, always
    if (!inControl)
      mSession->suppressTimeout(false);
    return;
  }
  mInControl = inControl;
  if (inControl) {
    // still in control (its end not sent yet): nothing to say
    if (mOperationDonePending)
      mOperationDonePending = false;
    else
      mSession->send(kDDesktopInControl);
  } else {
    // sent when nothing else follows (flushOperationDone): a 'disc' right
    // after it takes its place
    mOperationDonePending = true;
    mSession->suppressTimeout(false);
  }
}


void DockController::flushOperationDone()
{
  if (!mOperationDonePending)
    return;
  mOperationDonePending = false;
  mSession->send(kDOperationDone);        // the Newton doesn't answer it
}


void DockController::sendDisconnect()
{
  // 'disc' while the Newton's docker still reads commands (no 'opdn'
  // first): it ends the session at once and quietly (ConnectionDone(0)).
  // After an 'opdn' it goes back to the Connection slip, which reads the
  // 'disc' only when it idles (every 2 s, once it is open)
  mOperationDonePending = false;
  mSession->send(kDDisconnect);
}


void DockController::loadExtension(EventType inId)
{
  if (mExtensions.count(inId))
    return;
  std::vector<uint8_t> extension = Extension(inId);
  if (extension.empty())
    throw DockFailure(kDockErrDesktopError, "NCX has no extension '" + CmdName(inId) + "'");
  int32_t result = mSession->loadExtension(extension);
  // already there from an earlier session of this Newton: fine
  if (result != 0 && result != kDockErrProtocolExtAlreadyRegistered)
    throw DockFailure(result, "the Newton didn't take the extension '" + CmdName(inId) + "'");
  mExtensions.insert(inId);
}


void DockController::setStatus(const std::string & inText)
{
  if (mEvents.status)
    mEvents.status(inText);
}

} // namespace ncx
