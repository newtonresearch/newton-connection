/*
 File: Screenshot.cc

 Screenshots of the Newton's screen (Screenshot.h).
 */

#include "dock/Screenshot.h"
#include "dock/DockController.h"
#include "base/Log.h"
#include "comms/DockRefs.h"

#include <algorithm>

namespace ncx {

namespace {

const EventType kScreenshotExtension = Cmd("NCss");
const EventType kScreenshotAnswer = Cmd("NCsr");
const EventType kHideSlipExtension = Cmd("scrn");

/** Ask the 'NCss' extension: its answer (a frame, a binary) or NIL;
    outError: an error it or the Newton sent. */
Ref Ask(DockController & ioDock, RefArg inRequest, int & outError)
{
  outError = 0;
  DockEvent answer = ioDock.session().callExtension(kScreenshotExtension, inRequest);
  if (answer.command == kDResult) {
    outError = answer.value();
    return NILREF;
  }
  if (answer.command != kScreenshotAnswer) {
    outError = kDockErrProtocolError;
    return NILREF;
  }
  RefVar result(EventRef(answer));
  if (IsFrame(result) && ISINT(GetFrameSlot(result, MakeSymbol("error")))) {
    outError = (int)RINT(GetFrameSlot(result, MakeSymbol("error")));
    return NILREF;
  }
  return result;
}

Ref Request(const char * inOp)
{
  RefVar request(AllocateFrame());
  SetFrameSlot(request, MakeSymbol("op"), MakeSymbol(inOp));
  return request;
}

int IntSlot(RefArg inFrame, const char * inSlot)
{
  Ref value = GetFrameSlot(inFrame, MakeSymbol(inSlot));
  return ISINT(value) ? (int)RINT(value) : 0;
}

} // namespace


std::vector<uint8_t> GrayFromBits(const uint8_t * inBits, size_t inSize, int inWidth, int inHeight, int inRowBytes,
                                  int inDepth)
{
  std::vector<uint8_t> gray((size_t)inWidth * inHeight, 255);
  if (inDepth != 1 && inDepth != 2 && inDepth != 4 && inDepth != 8)
    return gray;
  int maxValue = (1 << inDepth) - 1;
  int perByte = 8 / inDepth;
  for (int y = 0; y < inHeight; ++y)
    for (int x = 0; x < inWidth; ++x) {
      size_t at = (size_t)y * inRowBytes + (size_t)x / perByte;
      if (at >= inSize)
        return gray;
      int shift = 8 - inDepth * (x % perByte + 1);
      int value = (inBits[at] >> shift) & maxValue;
      gray[(size_t)y * inWidth + x] = (uint8_t)(255 - value * 255 / maxValue);
    }
  return gray;
}


int ScreenshotSession::begin()
{
  mDock.setDesktopControl(true);
  mDock.loadExtension(kHideSlipExtension);
  mDock.loadExtension(kScreenshotExtension);
  // 'scrn' toggles the slip: dres 0, or -28010 without the Toolkit app
  DockEvent answer = mDock.session().callExtension(kHideSlipExtension, RefVar(NILREF));
  int error = answer.command == kDResult ? answer.value() : kDockErrProtocolError;
  if (error != 0) {
    mDock.setDesktopControl(false);
    return error;
  }
  // the user arranges the Newton's screen: no time limit meanwhile
  mDock.session().suppressTimeout(true);
  mActive = true;
  return 0;
}


int ScreenshotSession::take(Screenshot & outShot, Progress * ioProgress)
{
  // the exchanges have a time limit, the waiting for the user between them
  // doesn't
  struct TimeLimit
  {
    Session & session;
    explicit TimeLimit(Session & inSession) : session(inSession) { session.suppressTimeout(false); }
    ~TimeLimit() { session.suppressTimeout(true); }
  } timeLimit(mDock.session());
  int error;
  if (mBlock == 0) {
    // all at once, as the Cocoa app's 'snap'
    RefVar shot(Ask(mDock, RefVar(Request("take")), error));
    if (error != 0)
      return error;
    if (!IsFrame(shot))
      return kDockErrProtocolError;
    RefVar bits(GetFrameSlot(shot, MakeSymbol("theBits")));
    int width = IntSlot(shot, "right") - IntSlot(shot, "left"), height = IntSlot(shot, "bottom") - IntSlot(shot, "top");
    int rowBytes = IntSlot(shot, "rowBytes");
    if (!IsBinary(bits) || width <= 0 || height <= 0 || (int)Length(bits) < rowBytes * height)
      return kDockErrProtocolError;
    outShot.width = width;
    outShot.height = height;
    outShot.depth = IntSlot(shot, "depth");
    outShot.gray = GrayFromBits((const uint8_t *)BinaryData(bits), Length(bits), width, height, rowBytes, outShot.depth);
    if (ioProgress) {
      ioProgress->setTotal(Length(bits));
      ioProgress->setCompleted(Length(bits));
    }
    return 0;
  }

  // the screen, then a few rows at a time (each a shot of its own: nothing
  // stays on the Newton between them)
  RefVar screen(Ask(mDock, RefVar(Request("screen")), error));
  if (error != 0)
    return error;
  if (!IsFrame(screen))
    return kDockErrProtocolError;
  int left = IntSlot(screen, "left"), top = IntSlot(screen, "top"), right = IntSlot(screen, "right"),
      bottom = IntSlot(screen, "bottom");
  int width = right - left, height = bottom - top;
  int rowBytes = IntSlot(screen, "rowBytes"), depth = IntSlot(screen, "depth");
  if (width <= 0 || height <= 0 || rowBytes <= 0 || rowBytes * 8 < width * depth)
    return kDockErrProtocolError;
  int rowsAtATime = std::max(1, mBlock / rowBytes);
  std::vector<uint8_t> bits((size_t)rowBytes * height, 0);
  if (ioProgress) {
    ioProgress->setTotal((int64_t)bits.size());
    ioProgress->setCompleted(0);
  }
  for (int y = top; y < bottom; y += rowsAtATime) {
    int last = std::min(bottom, y + rowsAtATime);
    RefVar request(Request("rows"));
    SetFrameSlot(request, MakeSymbol("left"), MAKEINT(left));
    SetFrameSlot(request, MakeSymbol("top"), MAKEINT(y));
    SetFrameSlot(request, MakeSymbol("right"), MAKEINT(right));
    SetFrameSlot(request, MakeSymbol("bottom"), MAKEINT(last));
    RefVar rows(Ask(mDock, request, error));
    if (error != 0)
      return error;
    RefVar rowBits(IsFrame(rows) ? GetFrameSlot(rows, MakeSymbol("theBits")) : (Ref)NILREF);
    int bandRowBytes = IsFrame(rows) ? IntSlot(rows, "rowBytes") : 0;
    if (!IsBinary(rowBits) || bandRowBytes <= 0 || (int)Length(rowBits) < bandRowBytes * (last - y))
      return kDockErrProtocolError;
    const uint8_t * data = (const uint8_t *)BinaryData(rowBits);
    for (int row = 0; row < last - y; ++row)
      std::copy(data + (size_t)row * bandRowBytes, data + (size_t)row * bandRowBytes + std::min(rowBytes, bandRowBytes),
                bits.begin() + (ptrdiff_t)((size_t)(y - top + row) * rowBytes));
    if (ioProgress)
      ioProgress->setCompleted((int64_t)(last - top) * rowBytes);
  }

  outShot.width = width;
  outShot.height = height;
  outShot.depth = depth;
  outShot.gray = GrayFromBits(bits.data(), bits.size(), width, height, rowBytes, depth);
  return 0;
}


void ScreenshotSession::end()
{
  if (mActive) {
    // 'scrn' again: the slip is back
    mDock.session().callExtension(kHideSlipExtension, RefVar(NILREF));
    mActive = false;
  }
  mDock.setDesktopControl(false);
}

} // namespace ncx
