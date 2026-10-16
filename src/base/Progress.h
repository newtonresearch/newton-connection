/*
 File: Progress.h

 NSProgress for NCX's operations (PORTING.md 2.6): how much of how much is
 done (total -1: not known), what is happening, and whether the user wants
 it cancelled. Shared by the dock thread (which works and checks
 cancelled()) and the UI (which shows it and cancels); any thread may call
 anything.
 */

#ifndef NCX_BASE_PROGRESS_H
#define NCX_BASE_PROGRESS_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace ncx {

class Progress
{
public:
  explicit Progress(int64_t inTotal = -1) : mTotal(inTotal) { }

  void setTotal(int64_t inTotal) { mTotal = inTotal; }
  int64_t total() const { return mTotal; }

  void setCompleted(int64_t inCompleted) { mCompleted = inCompleted; }
  void add(int64_t inUnits) { mCompleted += inUnits; }
  int64_t completed() const { return mCompleted; }

  /** 0.0 ... 1.0; -1 if the total isn't known. */
  double fraction() const
  {
    int64_t total = mTotal, completed = mCompleted;
    if (total <= 0)
      return -1.0;
    return completed >= total ? 1.0 : (completed <= 0 ? 0.0 : double(completed) / double(total));
  }

  /** What is happening: "Backing up Names". */
  void setDescription(const std::string & inText)
  {
    std::lock_guard<std::mutex> lock(mLock);
    mDescription = inText;
  }
  std::string description() const
  {
    std::lock_guard<std::mutex> lock(mLock);
    return mDescription;
  }

  void cancel() { mCancelled = true; }
  bool cancelled() const { return mCancelled; }

private:
  std::atomic<int64_t> mTotal;
  std::atomic<int64_t> mCompleted { 0 };
  std::atomic<bool> mCancelled { false };
  mutable std::mutex mLock;
  std::string mDescription;
};

} // namespace ncx

#endif // NCX_BASE_PROGRESS_H
