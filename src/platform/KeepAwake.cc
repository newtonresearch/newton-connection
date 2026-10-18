/*
 File: KeepAwake.cc

 Keeping the computer awake while a transfer runs (KeepAwake.h).
 */

#include "platform/KeepAwake.h"

#include "base/Log.h"

#if defined(__APPLE__)
#include <IOKit/pwr_mgt/IOPMLib.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char ** environ;
#endif

namespace ncx {

#if defined(__APPLE__)

struct KeepAwake::Impl
{
  IOPMAssertionID assertion = kIOPMNullAssertionID;
};


KeepAwake::KeepAwake(const std::string & inWhy) : mImpl(new Impl())
{
  CFStringRef why = CFStringCreateWithCString(kCFAllocatorDefault, inWhy.c_str(), kCFStringEncodingUTF8);
  if (IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleSystemSleep, kIOPMAssertionLevelOn,
                                  why, &mImpl->assertion) != kIOReturnSuccess)
    mImpl->assertion = kIOPMNullAssertionID;
  if (why)
    CFRelease(why);
}


KeepAwake::~KeepAwake()
{
  if (mImpl->assertion != kIOPMNullAssertionID)
    IOPMAssertionRelease(mImpl->assertion);
}


bool KeepAwake::active() const
{
  return mImpl->assertion != kIOPMNullAssertionID;
}

#elif defined(_WIN32)

struct KeepAwake::Impl
{
  bool active = false;
};


KeepAwake::KeepAwake(const std::string &) : mImpl(new Impl())
{
  mImpl->active = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED) != 0;
}


KeepAwake::~KeepAwake()
{
  if (mImpl->active)
    SetThreadExecutionState(ES_CONTINUOUS);
}


bool KeepAwake::active() const
{
  return mImpl->active;
}

#else

// systemd-inhibit holds the inhibitor lock as long as the command it runs
// (sleep) runs: the command is ended when it isn't needed any more
struct KeepAwake::Impl
{
  pid_t child = -1;
};


KeepAwake::KeepAwake(const std::string & inWhy) : mImpl(new Impl())
{
  std::string why = "--why=" + inWhy;
  const char * argv[] = { "systemd-inhibit", "--what=idle:sleep", "--who=Newton Connection", why.c_str(),
                          "--mode=block", "sleep", "infinity", nullptr };
  pid_t child = -1;
  if (posix_spawnp(&child, "systemd-inhibit", nullptr, nullptr, const_cast<char **>(argv), environ) == 0)
    mImpl->child = child;
  else
    Log(kLogFull, "keep awake: no systemd-inhibit");
}


KeepAwake::~KeepAwake()
{
  if (mImpl->child > 0) {
    kill(mImpl->child, SIGTERM);
    int status = 0;
    waitpid(mImpl->child, &status, 0);
  }
}


bool KeepAwake::active() const
{
  return mImpl->child > 0;
}

#endif

} // namespace ncx
