/*
 File: Observers.h

 The Cocoa app's notifications from the dock to the UI (PORTING.md 2.6):
 kDockDid...Notification and kSerialPortChanged, with what they carried
 (the document, the operation, an error). Observers are called on the UI
 thread (RunOnMainThread, Threads.h); post() may come from any thread.
 */

#ifndef NCX_BASE_OBSERVERS_H
#define NCX_BASE_OBSERVERS_H

#include <functional>
#include <map>
#include <mutex>

namespace ncx {

enum class NotificationKind
{
  kRequestKeyboard,     // kDockDidRequestKeyboardNotification
  kConnectKeyboard,     // kDockDidConnectKeyboardNotification
  kScreenshot,          // kDockDidScreenshotNotification
  kOperation,           // kDockDidOperationNotification: operation, error
  kCancel,              // kDockDidCancelNotification: operation, error
  kDisconnect,          // kDockDidDisconnectNotification: error
  kSerialPortChanged    // kSerialPortChanged
};

struct Notification
{
  NotificationKind kind;
  void * document = nullptr;  // the document it is about (object:), or none
  int operation = 0;          // the activity (kBackupActivity, ...)
  int error = 0;
};

class Observers
{
public:
  using Callback = std::function<void(const Notification &)>;
  using Token = int;

  /** The app's (the dock posts to it, the UI observes it). */
  static Observers & Dock();

  /** inCallback for each notification of inKind from now on; the token removes it. */
  Token add(NotificationKind inKind, Callback inCallback);
  void remove(Token inToken);

  /** To the observers of its kind, on the UI thread. An observer removed
      before then isn't called. */
  void post(const Notification & inNotification);

private:
  void deliver(const Notification & inNotification);
  struct Entry { NotificationKind kind; Callback callback; };
  std::mutex mLock;
  std::map<Token, Entry> mEntries;
  Token mNext = 1;
};

} // namespace ncx

#endif // NCX_BASE_OBSERVERS_H
