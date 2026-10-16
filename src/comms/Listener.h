/*
 File: Listener.h

 Listening on every enabled transport at once (PORTING.md 3.2; the Cocoa
 app's NCEndpointController): the first Newton to connect wins, the other
 listeners are closed. The targets are newton_dock's (Matt/NTKTransport.h)
 and NCX's own:

   tcp-all:3679       TCP on all interfaces: a Newton docking over the
                      network (plain), or Einstein's serial port as a TCP
                      client (MNP); the first byte tells which
   tcp:<port>         the same, this machine only
   serial:<device>[@<bps>]   a serial port (MNP), 38400 bps unless given
   einstein-pipes     Einstein's named pipes (MNP; macOS)
 */

#ifndef NCX_COMMS_LISTENER_H
#define NCX_COMMS_LISTENER_H

#include "comms/Connection.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

class NTKTransport;

namespace ncx {

class Listener
{
public:
  Listener() { }
  ~Listener();
  Listener(const Listener &) = delete;
  Listener & operator=(const Listener &) = delete;

  /** Listen on the targets. Those that can't be used are logged and left
      out (their reasons in outError); false if none is left. */
  bool start(const std::vector<std::string> & inTargets, std::string & outError);

  /** What it listens on, described ("*:3679", "/dev/cu.usbserial at 38400
      bps", ...). */
  std::vector<std::string> listening() const;

  /** Wait at most inMilliseconds (-1: no limit) for a Newton. Its
      connection; the listeners are all closed then. nullptr after the
      time, after stop(), or when the connection can't be made (outError). */
  std::unique_ptr<Connection> waitForConnection(int inMilliseconds, std::string & outError);

  /** From any thread: waitForConnection returns (within 0.2 s). */
  void stop() { mStopping = true; }

  /** Close all listeners. */
  void close();

private:
  struct Entry
  {
    std::string target;
    NTKTransport * transport = nullptr;
    bool tcp = false;
  };
  std::vector<Entry> mEntries;
  std::atomic<bool> mStopping { false };
};

} // namespace ncx

#endif // NCX_COMMS_LISTENER_H
