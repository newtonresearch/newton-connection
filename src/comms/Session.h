/*
 File: Session.h

 The dock session (the Cocoa app's NCSession and NCCursor, PORTING.md 3.5):
 sending events and waiting for their replies, and the requests the dock's
 components make: the Newton's info, stores, soups, entries, queries,
 packages, protocol extensions, global functions. For the dock thread only;
 includes newton-framework's headers (Refs).

 Failures throw DockFailure: the connection is gone, no reply in time, or
 the wrong reply.
 */

#ifndef NCX_COMMS_SESSION_H
#define NCX_COMMS_SESSION_H

#include "comms/DockRefs.h"
#include "comms/EventQueue.h"

#include "base/Progress.h"
#include "base/Threads.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ncx {

/** A session that can't go on. code: a DockError (kDockErrDisconnected,
    kDockErrIdleTooLong, kDockErrProtocolError, ...) or the Newton's. */
struct DockFailure : std::runtime_error
{
  int code;
  DockFailure(int inCode, const std::string & inWhat) : std::runtime_error(inWhat), code(inCode) { }
};

class Cursor;

class Session
{
public:
  explicit Session(EventQueue & inQueue) : mQueue(inQueue) { }
  ~Session();

  EventQueue & queue() { return mQueue; }

  /*-- events --*/

  void send(const DockEvent & inEvent);
  void send(EventType inCommand) { send(DockEvent::Make(inCommand)); }
  void sendValue(EventType inCommand, int32_t inValue) { send(DockEvent::MakeValue(inCommand, inValue)); }
  void sendRef(EventType inCommand, RefArg inObj) { send(MakeRefEvent(inCommand, inObj)); }

  /** The next event from the Newton ('helo' skipped). inExpected: what it
      should be (kDAnyEvent: anything; a different one is logged and
      returned, but an error result throws: the Newton refused the last
      request). Throws when the connection is gone or nothing came in
      inSeconds (default kDefaultTimeout; -1: no limit, as while the user
      types on the keyboard). */
  DockEvent receive(EventType inExpected, int inSeconds = 0);

  /** A kDResult's error code (0: fine). Throws if something else comes. */
  int32_t receiveResult();

  DockEvent sendAndReceive(EventType inCommand, EventType inExpected)
  {
    send(inCommand);
    return receive(inExpected);
  }

  /** No timeout while waiting for the user (keyboard, screenshot). */
  void suppressTimeout(bool inSuppress) { mNoTimeout = inSuppress; }

  /** 'helo' to the Newton when nothing went over the wire for
      kDefaultTimeout seconds (the Newton ends a quiet session). */
  void startTickler();
  void stopTickler();

  /*-- information --*/

  Ref getUserFont();
  Ref getUserFolders();
  Ref getGestalt(uint32_t inSelector);
  /** The hexade (AviD's Fix2010) and how far the Newton's clock is from
      ours; for currentTime(). */
  void getHexade();
  Ref setDateTime(RefArg inMinutesSince1904);
  void setStatusText(const std::string & inText);
  /** The last sync time to remember (minutes since 1904); returns the
      Newton's current time (Fix2010 applied). */
  uint32_t setLastSyncTime(uint32_t inTime);
  /** The Newton's time now, as far as we know (minutes since 1904). */
  int32_t currentTime() const;

  /*-- stores --*/

  Ref getAllStores();
  Ref getDefaultStore();
  /** inStore NIL: the default store. */
  int32_t setCurrentStore(RefArg inStore, bool inSetStoreInfo);

  /*-- soups --*/

  /** [names, signatures] of the current store's soups. */
  Ref getAllSoups();
  Ref getSoupIndexes();
  Ref getSoupInfo();
  int32_t setSoupInfo(RefArg inSoupInfo);
  int32_t createSoup(RefArg inSoupName, RefArg inIndexes);
  int32_t deleteSoup();
  int32_t emptySoup();
  int32_t setCurrentSoup(RefArg inSoupName);

  /*-- entries (in the current soup) --*/

  /** Adds it, and sets its _uniqueId and _modTime. */
  int32_t addEntry(RefArg ioEntry);
  int32_t changeEntry(RefArg inEntry);
  int32_t deleteEntry(RefArg inEntry);
  int32_t deleteEntryList(RefArg inEntries);
  int32_t deleteEntryId(RefArg inId);
  int32_t deleteEntryIdList(RefArg inIds);
  Ref getEntry(int32_t inUniqueId);
  Ref getEntryIds();
  /** The ids of the entries changed since the last sync time
      (setLastSyncTime): NewtonOS 1's backup. */
  Ref getChangedIds();

  /*-- queries --*/

  std::unique_ptr<Cursor> query(RefArg inSoupName, RefArg inQuerySpec);

  /*-- packages --*/

  /** Send a package file (kDLoadPackage); progress: bytes the Newton has
      taken, of all; cancelled() is checked between chunks (the session
      can't go on then: it throws). */
  void sendPackage(const std::filesystem::path & inFile, Progress * ioProgress = nullptr);

  /*-- protocol extensions --*/

  /** inExtension: its id (4 bytes) and its code, as NCX's .stream files. */
  int32_t loadExtension(const std::vector<uint8_t> & inExtension);
  DockEvent callExtension(EventType inId, RefArg inArgs);
  int32_t removeExtension(EventType inId);

  /*-- global functions, root methods --*/

  Ref callGlobalFunction(const char * inName, RefArg inArgs);
  Ref callRootMethod(const char * inName, RefArg inArgs);

private:
  Ref callFunction(EventType inCommand, const char * inName, RefArg inArgs);
  int32_t deleteEntries(RefArg inIds);
  void sendWithProgress(const DockEvent & inEvent, Progress * ioProgress);

  /** Why the session can't go on: the connection is gone, after what. */
  std::string gone() const;

  EventQueue & mQueue;
  EventType mLastSent = 0;     // for the messages
  bool mNoTimeout = false;
  int mHexade = 0;
  int32_t mDelta = 0;          // the Newton's clock - ours (minutes)
  Timer mTickler;
};


/** A query's cursor on the Newton (NCCursor); freed on the Newton when
    destroyed. */
class Cursor
{
public:
  Cursor(Session & inSession) : mSession(inSession) { }
  ~Cursor();
  void query(RefArg inSoupName, RefArg inQuerySpec);
  int32_t countEntries();
  Ref gotoKey(RefArg inKey);
  Ref move(int32_t inOffset);
  Ref entry();
  Ref next();
  Ref prev();
  Ref reset();
  Ref resetToEnd();
  int32_t id() const { return mId; }
private:
  Ref common(EventType inCommand, bool inReset);
  Session & mSession;
  int32_t mId = 0;
};

} // namespace ncx

#endif // NCX_COMMS_SESSION_H
