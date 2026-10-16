/*
 File: Session.cc

 The dock session: requests and replies (Session.h).
 */

#include "comms/Session.h"
#include "base/Dates.h"
#include "base/Log.h"
#include "base/NewtonStrings.h"
#include "base/Strings.h"

#include "RSSymbols.h"

#include <fstream>
#include <iterator>
#include <thread>

namespace ncx {

namespace {

// AviD's Fix2010: the Newton's Time() repeats every hexade
const int32_t kTimeOfTranquilityBegin = 46811520;    // Jan 1, 1993 12 AM
const int32_t kTimeOfTranquilityEnd = 55226880;      // Jan 1, 2009 12 AM
const int32_t kLengthOfHexade = kTimeOfTranquilityEnd - kTimeOfTranquilityBegin;

void PutLong(std::vector<uint8_t> & ioBytes, uint32_t inValue)
{
  for (int shift = 24; shift >= 0; shift -= 8)
    ioBytes.push_back((uint8_t)(inValue >> shift));
}

void Append(std::vector<uint8_t> & ioBytes, const std::vector<uint8_t> & inMore)
{
  ioBytes.insert(ioBytes.end(), inMore.begin(), inMore.end());
}

/** A string's UniChars as the Newton has them (big-endian), with the 0;
    padded to 4 bytes if inPad. */
std::vector<uint8_t> BigEndianString(RefArg inStr, bool inPad, size_t inMaxBytes = 0)
{
  std::vector<uint8_t> bytes;
  if (!IsString(inStr))
    return bytes;
  const UniChar * chars = GetUString(inStr);
  size_t count = Length(inStr) / sizeof(UniChar);
  if (inMaxBytes > 0 && count * 2 > inMaxBytes)
    count = inMaxBytes / 2;
  for (size_t i = 0; i < count; ++i) {
    bytes.push_back((uint8_t)(chars[i] >> 8));
    bytes.push_back((uint8_t)chars[i]);
  }
  while (inPad && bytes.size() % 4 != 0)
    bytes.push_back(0);
  return bytes;
}

void ValidArg(bool inOk)
{
  if (!inOk)
    throw DockFailure(kDockErrBadEntry, "a bad argument to a dock request");
}

} // namespace


Session::~Session()
{
  stopTickler();
}


/*-- events ------------------------------------------------------------------*/

void Session::send(const DockEvent & inEvent)
{
  if (!mQueue.send(inEvent))
    throw DockFailure(kDockErrDisconnected, "the Newton is gone (" + mQueue.why() + ")");
}


DockEvent Session::receive(EventType inExpected, int inSeconds)
{
  int seconds = inSeconds == 0 ? kDefaultTimeout : inSeconds;
  if (mNoTimeout)
    seconds = -1;
  DockEvent event;
  for (;;) {
    EventQueue::Result result = mQueue.next(event, seconds < 0 ? -1 : seconds * 1000);
    if (result == EventQueue::kClosed)
      throw DockFailure(kDockErrDisconnected, "the Newton is gone (" + mQueue.why() + ")");
    if (result == EventQueue::kTimeout)
      throw DockFailure(kDockErrIdleTooLong, "no reply from the Newton (waiting for '" + CmdName(inExpected) + "')");
    if (event.command != kDHello)
      break;
  }
  if (inExpected != kDAnyEvent && event.command != inExpected)
    Log(kLogMinimum, "#### expected '%s', received '%s'", CmdName(inExpected).c_str(), CmdName(event.command).c_str());
  return event;
}


int32_t Session::receiveResult()
{
  DockEvent event = receive(kDResult);
  if (event.command != kDResult)
    throw DockFailure(kDockErrProtocolError, "'" + CmdName(event.command) + "' instead of a result");
  return event.value();
}


void Session::startTickler()
{
  // 'helo' if nothing went over the wire for kDefaultTimeout seconds; the
  // timer runs every second and looks
  auto last = std::make_shared<std::atomic<int64_t>>(Now());
  mTickler.start(1.0, [this, last] {
    if (mQueue.isClosed())
      return;
    if (Now() - *last >= kDefaultTimeout) {
      mQueue.send(DockEvent::Make(kDHello));
      *last = Now();
    }
  });
}


void Session::stopTickler()
{
  mTickler.stop();
}


/*-- information -------------------------------------------------------------*/

Ref Session::getUserFont()
{
  RefVar args(MakeArray(1));
  SetArraySlot(args, 0, SYMA(userFont));
  return callGlobalFunction("GetUserConfig", args);
}


Ref Session::getUserFolders()
{
  RefVar args(MakeArray(1));
  SetArraySlot(args, 0, SYMA(userFolders));
  return callGlobalFunction("GetUserConfig", args);
}


Ref Session::getGestalt(uint32_t inSelector)
{
  RefVar args(MakeArray(1));
  SetArraySlot(args, 0, MAKEINT(inSelector));
  return callGlobalFunction("Gestalt", args);
}


void Session::getHexade()
{
  RefVar args(MakeArray(1));
  SetArraySlot(args, 0, MakeSymbol("hexade"));
  Ref hexade = callGlobalFunction("GetUserConfig", args);
  mHexade = ISINT(hexade) ? (int)RVALUE(hexade) : 0;
  // and how far the Newton's clock is from ours
  int32_t now = (int32_t)NewtonFromTime(Now());
  mDelta = (int32_t)setLastSyncTime(0) - now;
}


Ref Session::setDateTime(RefArg inMinutesSince1904)
{
  RefVar args(MakeArray(1));
  SetArraySlot(args, 0, inMinutesSince1904);
  return callGlobalFunction("SetTime", args);
}


void Session::setStatusText(const std::string & inText)
{
  sendRef(kDSetStatusText, MakeStringFromUTF8(inText));
  receiveResult();
}


uint32_t Session::setLastSyncTime(uint32_t inTime)
{
  sendValue(kDLastSyncTime, (int32_t)inTime);
  DockEvent event = receive(kDCurrentTime);
  // the Newton gives Time() without Fix2010's hexade
  return (uint32_t)(event.value() + mHexade * kLengthOfHexade);
}


int32_t Session::currentTime() const
{
  return (int32_t)NewtonFromTime(Now()) + mDelta;
}


/*-- stores ------------------------------------------------------------------*/

Ref Session::getAllStores()
{
  return EventRef(sendAndReceive(kDGetStoreNames, kDStoreNames));
}


Ref Session::getDefaultStore()
{
  return EventRef(sendAndReceive(kDGetDefaultStore, kDDefaultStore));
}


int32_t Session::setCurrentStore(RefArg inStore, bool inSetStoreInfo)
{
  if (ISNIL(inStore)) {
    send(kDSetStoreToDefault);
  } else {
    RefVar store(AllocateFrame());
    SetFrameSlot(store, SYMA(name), GetFrameSlot(inStore, SYMA(name)));
    SetFrameSlot(store, SYMA(kind), GetFrameSlot(inStore, SYMA(kind)));
    SetFrameSlot(store, SYMA(signature), GetFrameSlot(inStore, SYMA(signature)));
    if (inSetStoreInfo)
      SetFrameSlot(store, SYMA(info), GetFrameSlot(inStore, SYMA(info)));
    sendRef(kDSetCurrentStore, store);
  }
  return receiveResult();
}


/*-- soups -------------------------------------------------------------------*/

Ref Session::getAllSoups()
{
  // the event: an array of names, an array of signatures
  DockEvent event = sendAndReceive(kDGetSoupNames, kDSoupNames);
  RefVar names(EventRef(event));
  RefVar signatures(EventRef2(event));
  if (!IsArray(names) || !IsArray(signatures))
    return NILREF;
  RefVar soups(MakeArray(2));
  SetArraySlot(soups, 0, names);
  SetArraySlot(soups, 1, signatures);
  return soups;
}


Ref Session::getSoupIndexes()
{
  return EventRef(sendAndReceive(kDGetIndexDescription, kDIndexDescription));
}


Ref Session::getSoupInfo()
{
  return EventRef(sendAndReceive(kDGetSoupInfo, kDSoupInfo));
}


int32_t Session::setSoupInfo(RefArg inSoupInfo)
{
  sendRef(kDSetSoupInfo, inSoupInfo);
  return receiveResult();
}


int32_t Session::createSoup(RefArg inSoupName, RefArg inIndexes)
{
  // the length of the name, the name (big-endian UniChars, padded), the indexes
  ValidArg(IsString(inSoupName));
  std::vector<uint8_t> name = BigEndianString(inSoupName, false);
  std::vector<uint8_t> data;
  PutLong(data, (uint32_t)name.size());
  Append(data, name);
  while (data.size() % 4 != 0)
    data.push_back(0);
  Append(data, FlattenToBytes(inIndexes));
  send(DockEvent::MakeData(kDCreateSoup, std::move(data)));
  return receiveResult();
}


int32_t Session::deleteSoup()
{
  send(kDDeleteSoup);
  return receiveResult();
}


int32_t Session::emptySoup()
{
  send(kDEmptySoup);
  return receiveResult();
}


int32_t Session::setCurrentSoup(RefArg inSoupName)
{
  // big-endian UniChars, at most 64 bytes (a soup name has at most 25 characters)
  ValidArg(IsString(inSoupName));
  send(DockEvent::MakeData(kDSetCurrentSoup, BigEndianString(inSoupName, false, 64)));
  return receiveResult();
}


/*-- entries -----------------------------------------------------------------*/

int32_t Session::addEntry(RefArg ioEntry)
{
  sendRef(kDAddEntry, ioEntry);
  DockEvent event = receive(kDAddedID);
  SetFrameSlot(ioEntry, SYMA(_uniqueId), MAKEINT(event.value()));
  SetFrameSlot(ioEntry, SYMA(_modTime), MAKEINT(currentTime()));
  return 0;
}


int32_t Session::changeEntry(RefArg inEntry)
{
  sendRef(kDChangedEntry, inEntry);
  return receiveResult();
}


int32_t Session::deleteEntry(RefArg inEntry)
{
  ValidArg(IsFrame(inEntry));
  RefVar id(GetFrameSlot(inEntry, SYMA(_uniqueId)));
  ValidArg(ISINT(id));
  RefVar ids(MakeArray(1));
  SetArraySlot(ids, 0, id);
  return deleteEntries(ids);
}


int32_t Session::deleteEntryList(RefArg inEntries)
{
  ValidArg(IsArray(inEntries));
  ArrayIndex count = Length(inEntries);
  RefVar ids(MakeArray(count));
  for (ArrayIndex i = 0; i < count; ++i) {
    RefVar entry(GetArraySlot(inEntries, i));
    ValidArg(IsFrame(entry));
    RefVar id(GetFrameSlot(entry, SYMA(_uniqueId)));
    ValidArg(ISINT(id));
    SetArraySlot(ids, i, id);
  }
  return deleteEntries(ids);
}


int32_t Session::deleteEntryId(RefArg inId)
{
  ValidArg(ISINT(inId));
  RefVar ids(MakeArray(1));
  SetArraySlot(ids, 0, inId);
  return deleteEntries(ids);
}


int32_t Session::deleteEntryIdList(RefArg inIds)
{
  ValidArg(IsArray(inIds));
  for (ArrayIndex i = 0, count = Length(inIds); i < count; ++i)
    ValidArg(ISINT(GetArraySlot(inIds, i)));
  return deleteEntries(inIds);
}


int32_t Session::deleteEntries(RefArg inIds)
{
  // the number of entries, their ids
  ArrayIndex count = Length(inIds);
  if (count == 0)
    return 0;
  std::vector<uint8_t> data;
  PutLong(data, count);
  for (ArrayIndex i = 0; i < count; ++i)
    PutLong(data, (uint32_t)RVALUE(GetArraySlot(inIds, i)));
  send(DockEvent::MakeData(kDDeleteEntries, std::move(data)));
  return receiveResult();
}


Ref Session::getEntry(int32_t inUniqueId)
{
  ValidArg(inUniqueId != 0);
  sendValue(kDReturnEntry, inUniqueId);
  return EventRef(receive(kDEntry));
}


Ref Session::getEntryIds()
{
  // the number of ids, the ids
  DockEvent event = sendAndReceive(kDGetSoupIDs, kDSoupIDs);
  uint32_t count = event.longAt(0);
  if (event.data.size() < 4 + (size_t)count * 4)
    return NILREF;
  RefVar ids(MakeArray(count));
  for (uint32_t i = 0; i < count; ++i)
    SetArraySlot(ids, i, MAKEINT((int32_t)event.longAt(4 + i * 4)));
  return ids;
}


/*-- queries -----------------------------------------------------------------*/

std::unique_ptr<Cursor> Session::query(RefArg inSoupName, RefArg inQuerySpec)
{
  ValidArg(ISNIL(inSoupName) || IsString(inSoupName));
  ValidArg(ISNIL(inQuerySpec) || IsFrame(inQuerySpec));
  auto cursor = std::make_unique<Cursor>(*this);
  cursor->query(inSoupName, inQuerySpec);
  return cursor;
}


Cursor::~Cursor()
{
  if (mId == 0)
    return;
  try {
    mSession.sendValue(kDCursorFree, mId);
    mSession.receiveResult();
  } catch (const DockFailure &) {
    // the session is gone: so is the cursor
  }
}


void Cursor::query(RefArg inSoupName, RefArg inQuerySpec)
{
  RefVar param(AllocateFrame());
  if (NOTNIL(inSoupName))
    SetFrameSlot(param, SYMA(soupName), inSoupName);
  SetFrameSlot(param, SYMA(querySpec), inQuerySpec);
  mSession.sendRef(kDQuery, param);
  mId = mSession.receive(kDLongData).value();
}


int32_t Cursor::countEntries()
{
  mSession.sendValue(kDCursorCountEntries, mId);
  return mSession.receive(kDLongData).value();
}


Ref Cursor::gotoKey(RefArg inKey)
{
  std::vector<uint8_t> data;
  PutLong(data, (uint32_t)mId);
  Append(data, FlattenToBytes(inKey));
  mSession.send(DockEvent::MakeData(kDCursorGotoKey, std::move(data)));
  return EventRef(mSession.receive(kDRefResult));
}


Ref Cursor::move(int32_t inOffset)
{
  std::vector<uint8_t> data;
  PutLong(data, (uint32_t)mId);
  PutLong(data, (uint32_t)inOffset);
  mSession.send(DockEvent::MakeData(kDCursorMove, std::move(data)));
  return EventRef(mSession.receive(kDRefResult));
}


Ref Cursor::entry()      { return common(kDCursorEntry, false); }
Ref Cursor::next()       { return common(kDCursorNext, false); }
Ref Cursor::prev()       { return common(kDCursorPrev, false); }
Ref Cursor::reset()      { return common(kDCursorReset, true); }
Ref Cursor::resetToEnd() { return common(kDCursorResetToEnd, true); }


Ref Cursor::common(EventType inCommand, bool inReset)
{
  mSession.sendValue(inCommand, mId);
  if (inReset) {
    mSession.receive(kDRefResult);
    mSession.sendValue(kDCursorEntry, mId);
  }
  return EventRef(mSession.receive(kDRefResult));
}


/*-- packages ----------------------------------------------------------------*/

void Session::sendWithProgress(const DockEvent & inEvent, Progress * ioProgress)
{
  // in pieces, each when the Newton has taken (MNP: acknowledged) most of
  // what went before, so the progress is what has arrived and a cancel
  // comes soon
  const size_t kPiece = 4096, kAhead = 8192;
  std::vector<uint8_t> bytes = inEvent.encode();
  if (ioProgress) {
    ioProgress->setTotal((int64_t)inEvent.data.size());
    ioProgress->setCompleted(0);
  }
  NCX_LOG_MINIMUM("-->      %s", inEvent.describe().c_str());
  size_t sent = 0;
  while (sent < bytes.size()) {
    if (ioProgress && ioProgress->cancelled())
      throw DockFailure(kDockErrAborted, "cancelled");
    while (mQueue.pending() > kAhead && !mQueue.isClosed())
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    size_t count = std::min(kPiece, bytes.size() - sent);
    std::vector<uint8_t> piece(bytes.begin() + (long)sent, bytes.begin() + (long)(sent + count));
    if (mQueue.isClosed() || !mQueue.sendBytes(piece))
      throw DockFailure(kDockErrDisconnected, "the Newton is gone (" + mQueue.why() + ")");
    sent += count;
    if (ioProgress) {
      size_t arrived = sent - std::min(sent, mQueue.pending());
      ioProgress->setCompleted((int64_t)std::min(arrived, inEvent.data.size()));
    }
  }
  while (ioProgress && mQueue.pending() > 0 && !mQueue.isClosed()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ioProgress->setCompleted((int64_t)(inEvent.data.size() - std::min(inEvent.data.size(), mQueue.pending())));
  }
  if (ioProgress)
    ioProgress->setCompleted((int64_t)inEvent.data.size());
}


void Session::sendPackage(const std::filesystem::path & inFile, Progress * ioProgress)
{
  std::ifstream in(inFile, std::ios::binary);
  if (!in)
    throw DockFailure(kDockErrFileNotFound, "can't read " + inFile.u8string());
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  sendWithProgress(DockEvent::MakeData(kDLoadPackage, std::move(data)), ioProgress);
}


/*-- protocol extensions -----------------------------------------------------*/

int32_t Session::loadExtension(const std::vector<uint8_t> & inExtension)
{
  send(DockEvent::MakeData(kDRegProtocolExtension, inExtension));
  return receiveResult();
}


DockEvent Session::callExtension(EventType inId, RefArg inArgs)
{
  if (ISNIL(inArgs))
    send(inId);
  else
    sendRef(inId, inArgs);
  return receive(kDAnyEvent);    // usually a kDResult
}


int32_t Session::removeExtension(EventType inId)
{
  sendValue(kDRemoveProtocolExtension, (int32_t)inId);
  return receiveResult();
}


/*-- global functions, root methods ------------------------------------------*/

Ref Session::callGlobalFunction(const char * inName, RefArg inArgs)
{
  return callFunction(kDCallGlobalFunction, inName, inArgs);
}


Ref Session::callRootMethod(const char * inName, RefArg inArgs)
{
  return callFunction(kDCallRootMethod, inName, inArgs);
}


Ref Session::callFunction(EventType inCommand, const char * inName, RefArg inArgs)
{
  // the name (a symbol), the arguments (an array); of no set length
  ValidArg(inName != nullptr && IsArray(inArgs));
  std::vector<uint8_t> data = FlattenToBytes(MakeSymbol(inName));
  Append(data, FlattenToBytes(inArgs));
  send(DockEvent::MakeData(inCommand, kIndeterminateLength, std::move(data)));
  DockEvent event = receive(kDAnyEvent);
  return event.command == kDCallResult ? EventRef(event) : (Ref)NILREF;
}

} // namespace ncx
