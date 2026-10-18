/*
 File: KeepAwake.h

 Keeping the computer awake while a transfer runs (PORTING.md 7.10): a
 backup or a ROM dump may take long without the user touching anything.
 While a KeepAwake lives the system doesn't sleep when idle (the display
 may). macOS: an IOKit power assertion; Windows: SetThreadExecutionState
 (for the thread: make it and let it go on the same one); Linux:
 systemd-inhibit, if it is there.
 */

#ifndef NCX_PLATFORM_KEEPAWAKE_H
#define NCX_PLATFORM_KEEPAWAKE_H

#include <memory>
#include <string>

namespace ncx {

class KeepAwake
{
public:
  /** inWhy: what the system shows the user ("Backing up a Newton"). */
  explicit KeepAwake(const std::string & inWhy);
  ~KeepAwake();
  KeepAwake(const KeepAwake &) = delete;
  KeepAwake & operator=(const KeepAwake &) = delete;

  /** It works on this system (tests). */
  bool active() const;

private:
  struct Impl;
  std::unique_ptr<Impl> mImpl;
};

} // namespace ncx

#endif // NCX_PLATFORM_KEEPAWAKE_H
