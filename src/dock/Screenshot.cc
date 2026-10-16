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
  RefVar takeRequest(Request("take"));
  if (mBlock == 0)
    SetFrameSlot(takeRequest, MakeSymbol("bits"), TRUEREF);
  RefVar info(Ask(mDock, takeRequest, error));
  if (error != 0)
    return error;
  if (!IsFrame(info))
    return kDockErrProtocolError;
  int width = IntSlot(info, "right") - IntSlot(info, "left");
  int height = IntSlot(info, "bottom") - IntSlot(info, "top");
  int rowBytes = IntSlot(info, "rowBytes"), depth = IntSlot(info, "depth"), size = IntSlot(info, "size");
  if (width <= 0 || height <= 0 || size <= 0 || rowBytes * height > size)
    return kDockErrProtocolError;
  if (ioProgress) {
    ioProgress->setTotal(size);
    ioProgress->setCompleted(0);
  }

  std::vector<uint8_t> bits;
  bits.reserve((size_t)size);
  RefVar allBits(mBlock == 0 ? GetFrameSlot(info, MakeSymbol("theBits")) : (Ref)NILREF);
  if (IsBinary(allBits)) {
    const uint8_t * data = (const uint8_t *)BinaryData(allBits);
    bits.assign(data, data + Length(allBits));
  }
  while ((int)bits.size() < size) {
    RefVar request(Request("read"));
    SetFrameSlot(request, MakeSymbol("offset"), MAKEINT((int)bits.size()));
    SetFrameSlot(request, MakeSymbol("length"), MAKEINT(std::min(mBlock, size - (int)bits.size())));
    RefVar block(Ask(mDock, request, error));
    if (error != 0)
      return error;
    if (!IsBinary(block) || Length(block) == 0)
      return kDockErrProtocolError;
    const uint8_t * data = (const uint8_t *)BinaryData(block);
    bits.insert(bits.end(), data, data + Length(block));
    if (ioProgress)
      ioProgress->setCompleted((int64_t)bits.size());
  }
  Ask(mDock, RefVar(Request("done")), error);

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
