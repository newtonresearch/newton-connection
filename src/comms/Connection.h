/*
 File: Connection.h

 A byte stream to a Newton (PORTING.md 3.1, 3.4): MNP on a serial port, on
 Einstein's TCP client or its named pipes (newton_dock's MNPLink), or plain
 TCP when a Newton docks over the network (no MNP there). Made by the
 Listener (Listener.h) once a Newton has connected.

 The dock thread waits on waitFd() (NTKWaitReadable, with whatever else it
 waits for), then takes what has come with receive(). send() never blocks
 on MNP (it queues), and waits while the wire is busy on plain TCP.
 */

#ifndef NCX_COMMS_CONNECTION_H
#define NCX_COMMS_CONNECTION_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class NTKTransport;

namespace ncx {

class Connection
{
public:
  virtual ~Connection() { }

  /** What to wait on for reading. */
  virtual int waitFd() const = 0;

  /** What has arrived, appended to ioData (never blocks). False when the
      connection is gone; outWhy says why. */
  virtual bool receive(std::vector<uint8_t> & ioData, std::string & outWhy) = 0;

  /** False if the connection is gone. */
  virtual bool send(const std::vector<uint8_t> & inData) = 0;

  /** Bytes sent but not acknowledged by the Newton yet (MNP); 0 otherwise. */
  virtual size_t pending() { return 0; }

  /** End it (MNP: with an LD). */
  virtual void close() = 0;

  /** "MNP on 127.0.0.1:3679 (127.0.0.1:61031)", for messages and the log. */
  virtual std::string describe() const = 0;
};

/** MNP on a started transport, which it owns from now on; inFirstBytes:
    what was read from it already. nullptr and a message on failure. */
std::unique_ptr<Connection> MakeMNPConnection(NTKTransport * inTransport, const std::vector<uint8_t> & inFirstBytes,
                                              std::string & outError);

/** The transport's bytes as they are (a Newton docking over TCP). */
std::unique_ptr<Connection> MakeRawConnection(NTKTransport * inTransport, const std::vector<uint8_t> & inFirstBytes);

/** Einstein's named pipes (macOS: ~/Library/Application Support/Einstein
    Emulator/ExtrSerPort{Send,Recv}), as a transport; nullptr elsewhere. */
NTKTransport * MakeEinsteinPipeTransport();

} // namespace ncx

#endif // NCX_COMMS_CONNECTION_H
