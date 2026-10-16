/*
 File: Observers.cc

 The dock's notifications to the UI (Observers.h).
 */

#include "base/Observers.h"
#include "base/Threads.h"

#include <vector>

namespace ncx {

Observers & Observers::Dock()
{
  static Observers dock;
  return dock;
}


Observers::Token Observers::add(NotificationKind inKind, Callback inCallback)
{
  std::lock_guard<std::mutex> lock(mLock);
  Token token = mNext++;
  mEntries[token] = Entry{ inKind, std::move(inCallback) };
  return token;
}


void Observers::remove(Token inToken)
{
  std::lock_guard<std::mutex> lock(mLock);
  mEntries.erase(inToken);
}


void Observers::post(const Notification & inNotification)
{
  RunOnMainThread([this, inNotification] { deliver(inNotification); });
}


void Observers::deliver(const Notification & inNotification)
{
  // the callbacks as they are now, called without the lock (they may add
  // and remove observers)
  std::vector<std::pair<Token, Callback>> callbacks;
  {
    std::lock_guard<std::mutex> lock(mLock);
    for (auto & entry : mEntries)
      if (entry.second.kind == inNotification.kind)
        callbacks.emplace_back(entry.first, entry.second.callback);
  }
  for (auto & callback : callbacks) {
    bool stillThere;
    {
      std::lock_guard<std::mutex> lock(mLock);
      stillThere = mEntries.count(callback.first) != 0;
    }
    if (stillThere)
      callback.second(inNotification);
  }
}

} // namespace ncx
