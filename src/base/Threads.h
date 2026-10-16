/*
 File: Threads.h

 What NCX's threads use instead of GCD (PORTING.md 2.6): a serial queue (a
 thread that runs tasks in order: the install, event, and io queues), a
 counting semaphore, a repeating timer (the Dock session's tickler), and a
 way to run something on the UI thread, which the UI provides
 (SetMainThreadRunner: FLTK's Fl::awake); without one (ncx-cli, tests) it
 runs at once on the calling thread.
 */

#ifndef NCX_BASE_THREADS_H
#define NCX_BASE_THREADS_H

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace ncx {

/** dispatch_semaphore: wait() takes one (waiting until there is one),
    signal() gives one. */
class Semaphore
{
public:
  explicit Semaphore(int inCount = 0) : mCount(inCount) { }
  void signal();
  void wait();
  /** False if none came within inMilliseconds. */
  bool waitFor(int inMilliseconds);
private:
  std::mutex mLock;
  std::condition_variable mChanged;
  int mCount;
};


/** dispatch_queue (serial): a thread of its own runs the tasks, one after
    the other, in the order they came. */
class SerialQueue
{
public:
  explicit SerialQueue(const std::string & inName);
  /** stop() */
  ~SerialQueue();

  const std::string & name() const { return mName; }

  /** Run inTask on the queue, later. Ignored after stop(). */
  void async(std::function<void()> inTask);

  /** Run inTask on the queue and wait until it has run. On the queue's
      own thread it runs at once (no deadlock). After stop(): at once, on
      this thread. */
  void sync(const std::function<void()> & inTask);

  /** True on the queue's own thread. */
  bool isCurrent() const;

  /** Run what is queued, then end the thread. Not on the queue's thread. */
  void stop();

private:
  void run();
  std::string mName;
  std::thread mThread;
  std::mutex mLock;
  std::condition_variable mChanged;
  std::deque<std::function<void()>> mTasks;
  bool mStopping = false;
};


/** dispatch_source timer: inTask every inSeconds (the first time after
    inSeconds), on the timer's own thread, until stop() or start() again. */
class Timer
{
public:
  Timer() { }
  ~Timer();
  void start(double inSeconds, std::function<void()> inTask);
  /** No more runs. Waits for a run in progress, unless called from it. */
  void stop();
  bool running();
private:
  void run(unsigned inGeneration, double inSeconds);
  std::mutex mLock;
  std::condition_variable mChanged;
  std::thread mThread;
  std::function<void()> mTask;
  unsigned mGeneration = 0;
  bool mRunning = false;
};


/** How the UI runs a function on its thread (FLTK: Fl::awake(...)); none:
    RunOnMainThread runs it at once. */
void SetMainThreadRunner(std::function<void(std::function<void()>)> inRunner);

/** inTask on the UI thread (dispatch_async(dispatch_get_main_queue(), ...)). */
void RunOnMainThread(std::function<void()> inTask);

} // namespace ncx

#endif // NCX_BASE_THREADS_H
