/*
 File: EventQueue.cc

 Dock events from a connection (EventQueue.h).
 */

#include "comms/EventQueue.h"
#include "base/Log.h"

#include "Matt/NTKTransport.h"

#include <chrono>

namespace ncx {

EventQueue::EventQueue(std::unique_ptr<Connection> inConnection)
  : mConnection(std::move(inConnection))
{
  mDescription = mConnection->describe();
  mReader = std::thread([this] { read(); });
}


EventQueue::~EventQueue()
{
  close();
}


void EventQueue::read()
{
  DockEventParser parser;
  std::vector<uint8_t> data;
  std::vector<DockEvent> events;
  std::string why;
  bool first = true;           // the listener may have read the first bytes already
  while (!mStopping) {
    int fd = mConnection->waitFd();
    bool ready = first;
    first = false;
    if (!ready && NTKWaitReadable(&fd, 1, 100, &ready) < 0) {
      why = "can't wait for the connection";
      break;
    }
    if (!ready)
      continue;
    data.clear();
    bool alive = mConnection->receive(data, why);
    if (!data.empty()) {
      events.clear();
      parser.feed(data.data(), data.size(), events);
      if (!events.empty()) {
        std::lock_guard<std::mutex> lock(mLock);
        for (DockEvent & event : events) {
          NCX_LOG_MINIMUM("     <-- %s", event.describe().c_str());
          mEvents.push_back(std::move(event));
        }
      }
      mChanged.notify_all();
    }
    if (!alive)
      break;
  }
  {
    std::lock_guard<std::mutex> lock(mLock);
    mWhy = mStopping ? std::string("closed") : (why.empty() ? std::string("the connection is gone") : why);
    mClosed = true;
  }
  mChanged.notify_all();
}


EventQueue::Result EventQueue::next(DockEvent & outEvent, int inMilliseconds)
{
  std::unique_lock<std::mutex> lock(mLock);
  auto ready = [this] { return !mEvents.empty() || mClosed; };
  if (inMilliseconds < 0)
    mChanged.wait(lock, ready);
  else if (!mChanged.wait_for(lock, std::chrono::milliseconds(inMilliseconds), ready))
    return kTimeout;
  if (mEvents.empty())
    return kClosed;
  outEvent = std::move(mEvents.front());
  mEvents.pop_front();
  return kEvent;
}


void EventQueue::add(const DockEvent & inEvent)
{
  {
    std::lock_guard<std::mutex> lock(mLock);
    mEvents.push_back(inEvent);
  }
  mChanged.notify_all();
}


bool EventQueue::send(const DockEvent & inEvent)
{
  if (mClosed)
    return false;
  NCX_LOG_MINIMUM("-->      %s", inEvent.describe().c_str());
  return mConnection->send(inEvent.encode());
}


bool EventQueue::sendBytes(const std::vector<uint8_t> & inBytes)
{
  return !mClosed && mConnection->send(inBytes);
}


size_t EventQueue::pending()
{
  return mClosed ? 0 : mConnection->pending();
}


void EventQueue::close(bool inByNewton)
{
  if (inByNewton && mReader.joinable()) {
    // what is queued goes first (MNP waits for its acknowledgement), then
    // the Newton drops the connection
    std::unique_lock<std::mutex> lock(mLock);
    mChanged.wait_for(lock, std::chrono::milliseconds(kNewtonEndsWait), [this] { return mClosed.load(); });
    if (!mClosed)
      NCX_LOG_MINIMUM("the Newton kept the connection after 'disc'; NCX ends it");
  }
  mStopping = true;
  if (mReader.joinable())
    mReader.join();
  if (mConnection)
    mConnection->close();
}


std::string EventQueue::why()
{
  std::lock_guard<std::mutex> lock(mLock);
  return mWhy;
}

} // namespace ncx
