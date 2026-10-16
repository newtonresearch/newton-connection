/*
 File: Listener.cc

 Listening on all transports at once (Listener.h).
 */

#include "comms/Listener.h"
#include "base/Log.h"

#include "Matt/NTKTransport.h"

#include <chrono>

namespace ncx {

Listener::~Listener()
{
  close();
}


void Listener::close()
{
  for (Entry & entry : mEntries)
    delete entry.transport;
  mEntries.clear();
}


bool Listener::start(const std::vector<std::string> & inTargets, std::string & outError)
{
  close();
  mStopping = false;
  outError.clear();
  for (const std::string & target : inTargets) {
    std::string error;
    NTKTransport * transport = (target == "einstein-pipes") ? MakeEinsteinPipeTransport()
                                                             : NTKTransport::Create(target, error);
    if (!transport && error.empty())
      error = "not on this system";
    if (transport && !transport->start(error)) {
      delete transport;
      transport = nullptr;
    }
    if (!transport) {
      Log(kLogMinimum, "Not listening on %s: %s", target.c_str(), error.c_str());
      outError += (outError.empty() ? "" : "; ") + target + ": " + error;
      continue;
    }
    Entry entry;
    entry.target = target;
    entry.transport = transport;
    entry.tcp = target.compare(0, 3, "tcp") == 0;
    mEntries.push_back(entry);
    NCX_LOG_MINIMUM("Listening on %s", transport->describe().c_str());
  }
  return !mEntries.empty();
}


std::vector<std::string> Listener::listening() const
{
  std::vector<std::string> names;
  for (const Entry & entry : mEntries)
    names.push_back(entry.transport->describe());
  return names;
}


std::unique_ptr<Connection> Listener::waitForConnection(int inMilliseconds, std::string & outError)
{
  using Clock = std::chrono::steady_clock;
  Clock::time_point end = Clock::now() + std::chrono::milliseconds(inMilliseconds < 0 ? 0 : inMilliseconds);
  outError.clear();
  while (!mStopping && !mEntries.empty()) {
    int wait = 200;
    if (inMilliseconds >= 0) {
      auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
      if (left <= 0)
        return nullptr;
      if (left < wait)
        wait = (int)left;
    }
    std::vector<int> fds;
    for (const Entry & entry : mEntries)
      fds.push_back(entry.transport->fd());
    std::unique_ptr<bool[]> ready(new bool[fds.size()]);
    int n = NTKWaitReadable(fds.data(), (int)fds.size(), wait, ready.get());
    if (n < 0) {
      outError = "can't wait for a connection";
      return nullptr;
    }
    for (size_t i = 0; i < mEntries.size(); ++i) {
      if (!ready[i])
        continue;
      Entry & entry = mEntries[i];
      std::vector<uint8_t> data;
      NTKTransport::Event event = entry.transport->handleReadable(data);
      bool decide = false;
      if (event == NTKTransport::kConnected && !entry.tcp)
        decide = true;                // a serial port or the pipes: its first bytes are here
      else if (event == NTKTransport::kConnected)
        NCX_LOG_MINIMUM("%s: connected from %s", entry.transport->describe().c_str(), entry.transport->peer().c_str());
      else if (event == NTKTransport::kData && entry.tcp && !data.empty())
        decide = true;                // TCP's first bytes: MNP or not
      if (!decide)
        continue;

      // this one: take it out, close the others
      NTKTransport * transport = entry.transport;
      bool mnp = !entry.tcp || data[0] != 'n';   // a plain dock event begins 'newtdock'; MNP with SYN
      entry.transport = nullptr;
      mEntries.erase(mEntries.begin() + (long)i);
      close();
      if (!mnp) {
        NCX_LOG_MINIMUM("A Newton docks over TCP (%s)", transport->peer().c_str());
        return MakeRawConnection(transport, data);
      }
      std::unique_ptr<Connection> connection = MakeMNPConnection(transport, data, outError);
      if (connection)
        NCX_LOG_MINIMUM("A Newton connects: %s", connection->describe().c_str());
      return connection;
    }
  }
  return nullptr;
}

} // namespace ncx
