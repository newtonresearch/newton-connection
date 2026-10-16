/*
 File: Screenshot.h

 Screenshots of the Newton's screen (PORTING.md 5.2.1; the Cocoa app's
 NCScreenshotComponent). The Newton needs the Toolkit app (NTK's
 "newtoolspro"): its ScreenShotFn takes the shot. NCX's 'NCss' extension
 takes it a few rows at a time (the Cocoa app's 'snap' sent it in one piece,
 too much for a real Newton's serial link, and a lot for its heap); 'scrn'
 hides the Dock slip meanwhile.

 The Newton's way, as the UI does it: begin() (the slip goes away, the user
 arranges the screen), take() as often as wanted, end() (the slip is back,
 the desktop out of control). On the dock thread.
 */

#ifndef NCX_DOCK_SCREENSHOT_H
#define NCX_DOCK_SCREENSHOT_H

#include "base/Progress.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ncx {

class DockController;

struct Screenshot
{
  int width = 0;
  int height = 0;
  int depth = 0;                  // bits a pixel on the Newton (1, 2, 4, 8)
  std::vector<uint8_t> gray;      // width * height pixels, 0 black, 255 white
};

/** A Newton bitmap's pixels (inDepth bits each, the first in the high bits
    of a byte; 0 white, the highest value black) as 8-bit gray. */
std::vector<uint8_t> GrayFromBits(const uint8_t * inBits, size_t inSize, int inWidth, int inHeight, int inRowBytes,
                                  int inDepth);

class ScreenshotSession
{
public:
  static constexpr int kBlock = 2048;       // bytes an answer, about (as newtdump)

  explicit ScreenshotSession(DockController & inDock) : mDock(inDock) { }

  /** Bytes an answer (kBlock; 64 ... 16384: as many rows as fit, at least
      one), or 0: the whole screen in one piece, as the Cocoa app's 'snap'
      (to compare). */
  void setBlock(int inBytes) { mBlock = inBytes <= 0 ? 0 : (inBytes < 64 ? 64 : (inBytes > 16384 ? 16384 : inBytes)); }

  /** Take control, hide the slip. 0, or kDockErrFileNotFound (-28010) if the
      Newton has no Toolkit app (the desktop is out of control again). */
  int begin();
  /** A shot: 0 and outShot, or the Newton's error. ioProgress: the bytes
      received of the screen's. */
  int take(Screenshot & outShot, Progress * ioProgress = nullptr);
  /** Show the slip again, give up control. */
  void end();

private:
  DockController & mDock;
  int mBlock = kBlock;
  bool mActive = false;
};

} // namespace ncx

#endif // NCX_DOCK_SCREENSHOT_H
