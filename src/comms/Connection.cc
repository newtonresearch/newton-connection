/*
 File: Connection.cc

 MNP and plain connections, Einstein's named pipes (Connection.h).
 */

#include "comms/Connection.h"
#include "base/Log.h"

#include "Matt/MNP.h"
#include "Matt/NTKTransport.h"

#include <chrono>
#include <thread>

#if defined(__APPLE__)
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ncx {

namespace {

/*--- MNP --------------------------------------------------------------------*/

class MNPConnection : public Connection
{
public:
  bool start(NTKTransport * inTransport, const std::vector<uint8_t> & inFirstBytes, std::string & outError)
  {
    mDescription = "MNP on " + inTransport->describe();
    if (inTransport->isConnected() && inTransport->peer() != inTransport->describe())
      mDescription += " (" + inTransport->peer() + ")";
    return mLink.start(inTransport, inFirstBytes, outError);
  }

  ~MNPConnection() override { mLink.stop(); }

  int waitFd() const override { return mLink.notifyFd(); }

  bool receive(std::vector<uint8_t> & ioData, std::string & outWhy) override
  {
    MNPLink::Event event;
    while (mLink.nextEvent(event)) {
      switch (event.kind) {
        case MNPLink::Event::kLinkUp:
          NCX_LOG_MINIMUM("MNP: the link is up");
          break;
        case MNPLink::Event::kData:
          ioData.insert(ioData.end(), event.data.begin(), event.data.end());
          break;
        case MNPLink::Event::kLinkDown:
          mGone = true;
          mWhy = event.text;
          break;
        default:
          break;
      }
    }
    outWhy = mWhy;
    return !mGone;
  }

  bool send(const std::vector<uint8_t> & inData) override
  {
    if (mGone)
      return false;
    mLink.send(inData);
    return true;
  }

  size_t pending() override { return mLink.pending(); }

  void close() override
  {
    // what is queued first (a last 'disc'): the LD would drop it
    for (int i = 0; i < 300 && !mGone && mLink.isUp() && mLink.pending() > 0; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!mGone && mLink.isUp())
      mLink.disconnect();
    mLink.stop();
    mGone = true;
  }

  std::string describe() const override { return mDescription; }

private:
  MNPLink mLink;
  bool mGone = false;
  std::string mWhy;
  std::string mDescription;
};


/*--- plain bytes (a Newton docking over TCP) --------------------------------*/

class RawConnection : public Connection
{
public:
  RawConnection(NTKTransport * inTransport, const std::vector<uint8_t> & inFirstBytes)
    : mTransport(inTransport), mFirstBytes(inFirstBytes)
  {
    mDescription = "TCP " + inTransport->describe() + " (" + inTransport->peer() + ")";
  }

  ~RawConnection() override { close(); }

  int waitFd() const override { return mTransport ? mTransport->fd() : -1; }

  bool receive(std::vector<uint8_t> & ioData, std::string & outWhy) override
  {
    if (!mFirstBytes.empty()) {
      ioData.insert(ioData.end(), mFirstBytes.begin(), mFirstBytes.end());
      mFirstBytes.clear();
    }
    if (!mTransport) {
      outWhy = "closed";
      return false;
    }
    std::vector<uint8_t> data;
    switch (mTransport->handleReadable(data)) {
      case NTKTransport::kData:
        ioData.insert(ioData.end(), data.begin(), data.end());
        return true;
      case NTKTransport::kDisconnected:
        outWhy = "the Newton disconnected";
        close();
        return false;
      default:
        return true;
    }
  }

  bool send(const std::vector<uint8_t> & inData) override
  {
    return mTransport && mTransport->send(inData.data(), inData.size());
  }

  void close() override
  {
    delete mTransport;      // closes the connection and the listener
    mTransport = nullptr;
  }

  std::string describe() const override { return mDescription; }

private:
  NTKTransport * mTransport;
  std::vector<uint8_t> mFirstBytes;
  std::string mDescription;
};


#if defined(__APPLE__)

/*--- Einstein's named pipes (macOS) -----------------------------------------*/

class EinsteinPipeTransport : public NTKTransport
{
public:
  ~EinsteinPipeTransport() override { disconnect(); }

  bool start(std::string & outError) override
  {
    const char * home = std::getenv("HOME");
    if (!home) {
      outError = "no home folder";
      return false;
    }
    std::string folder = std::string(home) + "/Library/Application Support/Einstein Emulator";
    mkdir(folder.c_str(), S_IRWXU);
    // the names are Einstein's: we read what it sends, and write what it receives
    std::string sendPipe = folder + "/ExtrSerPortSend", recvPipe = folder + "/ExtrSerPortRecv";
    for (const std::string & pipe : { sendPipe, recvPipe })
      if (access(pipe.c_str(), R_OK | W_OK) != 0 && mkfifo(pipe.c_str(), S_IRUSR | S_IWUSR) != 0) {
        outError = "can't make the pipe " + pipe + ": " + std::strerror(errno);
        return false;
      }
    mRead = open(sendPipe.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    mWrite = open(recvPipe.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (mRead < 0 || mWrite < 0) {
      outError = std::string("can't open Einstein's pipes: ") + std::strerror(errno);
      disconnect();
      return false;
    }
    mConnected = false;
    return true;
  }

  int fd() const override { return mRead; }

  Event handleReadable(std::vector<uint8_t> & outData) override
  {
    outData.clear();
    uint8_t buffer[4096];
    ssize_t n = read(mRead, buffer, sizeof(buffer));
    if (n <= 0)
      return kNothing;      // a pipe opened for writing too never ends
    outData.assign(buffer, buffer + n);
    if (!mConnected) {
      mConnected = true;
      return kConnected;    // with its first bytes, as a serial port
    }
    return kData;
  }

  bool isConnected() const override { return mConnected; }

  bool send(const uint8_t * inData, size_t inSize) override
  {
    while (inSize > 0) {
      ssize_t n = write(mWrite, inData, inSize);
      if (n > 0) {
        inData += n;
        inSize -= (size_t)n;
      } else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
        usleep(1000);       // the pipe is full: Einstein reads it soon
      } else {
        return false;
      }
    }
    return true;
  }

  void disconnect() override
  {
    if (mRead >= 0)
      close(mRead);
    if (mWrite >= 0)
      close(mWrite);
    mRead = mWrite = -1;
    mConnected = false;
  }

  std::string describe() const override { return "Einstein's pipes"; }

private:
  int mRead = -1, mWrite = -1;
  bool mConnected = false;
};

#endif

} // namespace


std::unique_ptr<Connection> MakeMNPConnection(NTKTransport * inTransport, const std::vector<uint8_t> & inFirstBytes,
                                              std::string & outError)
{
  auto connection = std::make_unique<MNPConnection>();
  if (!connection->start(inTransport, inFirstBytes, outError))
    return nullptr;
  return connection;
}


std::unique_ptr<Connection> MakeRawConnection(NTKTransport * inTransport, const std::vector<uint8_t> & inFirstBytes)
{
  return std::make_unique<RawConnection>(inTransport, inFirstBytes);
}


NTKTransport * MakeEinsteinPipeTransport()
{
#if defined(__APPLE__)
  return new EinsteinPipeTransport();
#else
  return nullptr;
#endif
}

} // namespace ncx
