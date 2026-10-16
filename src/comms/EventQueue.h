/*
 File: EventQueue.h

 The Cocoa app's NCDockEventQueue (PORTING.md 3.5): a thread of its own
 reads the connection and turns its bytes into dock events, which the dock
 thread takes in order (next). Events from the desktop (the UI asks for a
 backup, ...) join the same queue (add). Sending goes straight to the
 connection.
 */

#ifndef NCX_COMMS_EVENTQUEUE_H
#define NCX_COMMS_EVENTQUEUE_H

#include "comms/Connection.h"
#include "comms/DockEvent.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace ncx {

class EventQueue
{
public:
  /** Takes the connection and starts reading it. */
  explicit EventQueue(std::unique_ptr<Connection> inConnection);
  /** close() */
  ~EventQueue();
  EventQueue(const EventQueue &) = delete;
  EventQueue & operator=(const EventQueue &) = delete;

  enum Result { kEvent, kTimeout, kClosed };

  /** The next event: waits at most inMilliseconds (-1: no limit). kClosed
      once the connection is gone and all its events are taken (why()). */
  Result next(DockEvent & outEvent, int inMilliseconds);

  /** An event from the desktop, after those already queued. */
  void add(const DockEvent & inEvent);

  /** To the Newton. False if the connection is gone. */
  bool send(const DockEvent & inEvent);

  /** Encoded bytes to the Newton (a big event in pieces, Session's
      sendPackage). */
  bool sendBytes(const std::vector<uint8_t> & inBytes);

  /** Bytes sent but not acknowledged yet (MNP). */
  size_t pending();

  /** End the connection (MNP: with an LD) and the reading. */
  void close();

  bool isClosed() const { return mClosed; }
  std::string why();
  std::string describe() const { return mDescription; }

private:
  void read();
  std::unique_ptr<Connection> mConnection;
  std::string mDescription;
  std::thread mReader;
  std::mutex mLock;
  std::condition_variable mChanged;
  std::deque<DockEvent> mEvents;
  std::string mWhy;
  std::atomic<bool> mStopping { false };
  std::atomic<bool> mClosed { false };
};

} // namespace ncx

#endif // NCX_COMMS_EVENTQUEUE_H
