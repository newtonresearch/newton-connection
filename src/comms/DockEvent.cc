/*
 File: DockEvent.cc

 Docking Protocol events: making, encoding, describing, parsing
 (DockEvent.h).
 */

#include "comms/DockEvent.h"

#include <cstdio>
#include <cstring>

namespace ncx {

namespace {

const uint8_t kDockSignature[8] = { 'n', 'e', 'w', 't', 'd', 'o', 'c', 'k' };

void PutLong(std::vector<uint8_t> & ioBytes, uint32_t inValue)
{
  for (int shift = 24; shift >= 0; shift -= 8)
    ioBytes.push_back((uint8_t)(inValue >> shift));
}

} // namespace


std::string CmdName(EventType inCommand)
{
  std::string name;
  for (int shift = 24; shift >= 0; shift -= 8) {
    char c = (char)(inCommand >> shift);
    name += (c >= 32 && c < 127) ? c : '.';
  }
  return name;
}


std::string DockErrorText(int inError)
{
  static const struct { int error; const char * text; } kTexts[] = {
    { kDockErrBadStoreSignature, "bad store signature" },
    { kDockErrBadEntry, "bad entry" },
    { kDockErrAborted, "aborted" },
    { kDockErrBadQuery, "bad query" },
    { kDockErrReadEntryError, "error reading an entry" },
    { kDockErrBadCurrentSoup, "no current soup" },
    { kDockErrBadCommandLength, "bad command length" },
    { kDockErrEntryNotFound, "entry not found" },
    { kDockErrBadConnection, "bad connection" },
    { kDockErrFileNotFound, "file not found" },
    { kDockErrIncompatibleProtocol, "incompatible protocol" },
    { kDockErrProtocolError, "protocol error" },
    { kDockErrDockingCanceled, "docking cancelled" },
    { kDockErrStoreNotFound, "store not found" },
    { kDockErrSoupNotFound, "soup not found" },
    { kDockErrBadHeader, "bad header" },
    { kDockErrOutOfMemory, "out of memory" },
    { kDockErrNewtonVersionTooNew, "Newton version too new" },
    { kDockErrPackageCantLoad, "the package can't be loaded" },
    { kDockErrProtocolExtAlreadyRegistered, "protocol extension already registered" },
    { kDockErrRemoteImportError, "import error" },
    { kDockErrBadPasswordError, "bad password" },
    { kDockErrRetryPW, "retry the password" },
    { kDockErrIdleTooLong, "idle too long" },
    { kDockErrOutOfPower, "out of power" },
    { kDockErrBadCursor, "bad cursor" },
    { kDockErrAlreadyBusy, "already busy" },
    { kDockErrDesktopError, "desktop error" },
    { kDockErrCantConnectToModem, "can't connect to the modem" },
    { kDockErrDisconnected, "disconnected" },
    { kDockErrAccessDenied, "access denied" },
    { kDockErrDisconnectDuringRead, "disconnected while reading" },
    { kDockErrReadFailed, "read failed" },
    { kDockErrCommunicationsToolNotFound, "communications tool not found" },
    { kDockErrInvalidModemToolVersion, "invalid modem tool version" },
    { kDockErrCardNotInstalled, "card not installed" },
    { kDockErrBrowserFileNotFound, "file not found (browsing)" },
    { kDockErrBrowserVolumeNotFound, "volume not found (browsing)" },
    { kDockErrBrowserPathNotFound, "path not found (browsing)" },
  };
  for (const auto & entry : kTexts)
    if (entry.error == inError)
      return std::to_string(inError) + " (" + entry.text + ")";
  return std::to_string(inError);
}


DockEvent DockEvent::Make(EventType inCommand)
{
  return DockEvent(inCommand);
}


DockEvent DockEvent::MakeValue(EventType inCommand, int32_t inValue)
{
  DockEvent event(inCommand);
  PutLong(event.data, (uint32_t)inValue);
  event.length = 4;
  return event;
}


DockEvent DockEvent::MakeData(EventType inCommand, const void * inData, size_t inSize)
{
  const uint8_t * p = static_cast<const uint8_t *>(inData);
  return MakeData(inCommand, std::vector<uint8_t>(p, p + inSize));
}


DockEvent DockEvent::MakeData(EventType inCommand, std::vector<uint8_t> inData)
{
  uint32_t length = (uint32_t)inData.size();
  return MakeData(inCommand, length, std::move(inData));
}


DockEvent DockEvent::MakeData(EventType inCommand, uint32_t inLength, std::vector<uint8_t> inData)
{
  DockEvent event(inCommand);
  event.length = inLength;
  event.data = std::move(inData);
  return event;
}


uint32_t DockEvent::longAt(size_t inOffset) const
{
  if (inOffset + 4 > data.size())
    return 0;
  return (uint32_t)data[inOffset] << 24 | (uint32_t)data[inOffset + 1] << 16
       | (uint32_t)data[inOffset + 2] << 8 | (uint32_t)data[inOffset + 3];
}


std::vector<uint8_t> DockEvent::encode() const
{
  std::vector<uint8_t> bytes(kDockSignature, kDockSignature + 8);
  bytes.reserve(16 + data.size() + 3);
  PutLong(bytes, command);
  PutLong(bytes, length);
  bytes.insert(bytes.end(), data.begin(), data.end());
  while (bytes.size() % 4 != 0)
    bytes.push_back(0);
  return bytes;
}


std::string DockEvent::describe() const
{
  std::string text = CmdName(command);
  if (length == kIndeterminateLength)
    text += " [*" + std::to_string(data.size()) + "]";
  else
    text += " [" + std::to_string(length) + "]";
  if (data.size() == 4) {
    text += " " + std::to_string(value());
  } else {
    char hex[4];
    for (size_t i = 0; i < data.size() && i < 32; ++i) {
      std::snprintf(hex, sizeof(hex), " %02X", data[i]);
      text += hex;
    }
    if (data.size() > 32)
      text += " ...";
  }
  return text;
}


bool DockEventParser::openEnded(EventType & outCommand, const std::vector<uint8_t> * & outData) const
{
  if (mState != kOpenEnded)
    return false;
  outCommand = mEvent.command;
  outData = &mEvent.data;
  return true;
}


void DockEventParser::endOpenEnded(size_t inLength, std::vector<DockEvent> & outEvents)
{
  if (mState != kOpenEnded)
    return;
  if (inLength < mEvent.data.size())
    mEvent.data.resize(inLength);
  outEvents.push_back(std::move(mEvent));
  reset();
}


void DockEventParser::reset()
{
  mState = kSignature;
  mCount = 0;
  mEvent = DockEvent();
}


void DockEventParser::feed(const uint8_t * inData, size_t inSize, std::vector<DockEvent> & outEvents)
{
  for (size_t i = 0; i < inSize; ++i) {
    uint8_t b = inData[i];
    switch (mState) {
      case kSignature:
        if (b == kDockSignature[mCount]) {
          if (++mCount == 8) {
            mState = kCommand;
            mCount = 0;
            mEvent = DockEvent();
          }
        } else {
          mCount = (b == kDockSignature[0]) ? 1 : 0;     // resynchronise
        }
        break;
      case kCommand:
        mEvent.command = mEvent.command << 8 | b;
        if (++mCount == 4) {
          mState = kLength;
          mCount = 0;
        }
        break;
      case kLength:
        mEvent.length = mEvent.length << 8 | b;
        if (++mCount == 4) {
          mCount = 0;
          if (mEvent.length == kIndeterminateLength) {
            mState = kOpenEnded;
          } else if (mEvent.length == 0) {
            outEvents.push_back(std::move(mEvent));
            reset();
          } else {
            mPadded = ((size_t)mEvent.length + 3) & ~(size_t)3;
            mEvent.data.reserve(mEvent.length);
            mState = kData;
          }
        }
        break;
      case kData:
        if (mCount < mEvent.length)
          mEvent.data.push_back(b);
        if (++mCount == mPadded) {
          outEvents.push_back(std::move(mEvent));
          reset();
        }
        break;
      case kOpenEnded:
        // the data until the next signature: keep the bytes that may begin
        // one apart (mCount of them, at the end of the data) until it's sure
        mEvent.data.push_back(b);
        if (b == kDockSignature[mCount]) {
          if (++mCount == 8) {
            mEvent.data.resize(mEvent.data.size() - 8);
            outEvents.push_back(std::move(mEvent));
            mState = kCommand;
            mCount = 0;
            mEvent = DockEvent();
          }
        } else {
          mCount = (b == kDockSignature[0]) ? 1 : 0;
        }
        break;
    }
  }
}

} // namespace ncx
