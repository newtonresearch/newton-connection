/*
 File: Threads.cc

 Serial queues, semaphores, timers, the UI thread (Threads.h).
 */

#include "base/Threads.h"

#include <chrono>

namespace ncx {

/*-- Semaphore ---------------------------------------------------------------*/

void Semaphore::signal()
{
  {
    std::lock_guard<std::mutex> lock(mLock);
    ++mCount;
  }
  mChanged.notify_one();
}


void Semaphore::wait()
{
  std::unique_lock<std::mutex> lock(mLock);
  mChanged.wait(lock, [this] { return mCount > 0; });
  --mCount;
}


bool Semaphore::waitFor(int inMilliseconds)
{
  std::unique_lock<std::mutex> lock(mLock);
  if (!mChanged.wait_for(lock, std::chrono::milliseconds(inMilliseconds), [this] { return mCount > 0; }))
    return false;
  --mCount;
  return true;
}


/*-- SerialQueue -------------------------------------------------------------*/

SerialQueue::SerialQueue(const std::string & inName)
  : mName(inName)
{
  mThread = std::thread([this] { run(); });
}


SerialQueue::~SerialQueue()
{
  stop();
}


void SerialQueue::async(std::function<void()> inTask)
{
  {
    std::lock_guard<std::mutex> lock(mLock);
    if (mStopping)
      return;
    mTasks.push_back(std::move(inTask));
  }
  mChanged.notify_one();
}


void SerialQueue::sync(const std::function<void()> & inTask)
{
  if (isCurrent()) {
    inTask();
    return;
  }
  Semaphore done;
  bool queued = false;
  {
    std::lock_guard<std::mutex> lock(mLock);
    if (!mStopping) {
      mTasks.push_back([&] { inTask(); done.signal(); });
      queued = true;
    }
  }
  if (!queued) {
    inTask();                 // stopped: here
    return;
  }
  mChanged.notify_one();
  done.wait();                // run() runs all that was queued before it ends
}


bool SerialQueue::isCurrent() const
{
  return std::this_thread::get_id() == mThread.get_id();
}


void SerialQueue::stop()
{
  {
    std::lock_guard<std::mutex> lock(mLock);
    mStopping = true;
  }
  mChanged.notify_one();
  if (mThread.joinable() && !isCurrent())
    mThread.join();
}


void SerialQueue::run()
{
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(mLock);
      mChanged.wait(lock, [this] { return mStopping || !mTasks.empty(); });
      if (mTasks.empty())
        return;               // stopping, and nothing left
      task = std::move(mTasks.front());
      mTasks.pop_front();
    }
    task();
  }
}


/*-- Timer -------------------------------------------------------------------*/

Timer::~Timer()
{
  stop();
}


void Timer::start(double inSeconds, std::function<void()> inTask)
{
  stop();
  std::lock_guard<std::mutex> lock(mLock);
  mTask = std::move(inTask);
  mRunning = true;
  unsigned generation = ++mGeneration;
  mThread = std::thread([this, generation, inSeconds] { run(generation, inSeconds); });
}


void Timer::stop()
{
  std::thread thread;
  {
    std::lock_guard<std::mutex> lock(mLock);
    ++mGeneration;
    mRunning = false;
    thread = std::move(mThread);
  }
  mChanged.notify_all();
  if (thread.joinable()) {
    if (thread.get_id() == std::this_thread::get_id())
      thread.detach();        // stopped from its own task: it ends by itself
    else
      thread.join();
  }
}


bool Timer::running()
{
  std::lock_guard<std::mutex> lock(mLock);
  return mRunning;
}


void Timer::run(unsigned inGeneration, double inSeconds)
{
  auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(inSeconds));
  auto next = std::chrono::steady_clock::now() + interval;
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(mLock);
      if (mChanged.wait_until(lock, next, [&] { return mGeneration != inGeneration; }))
        return;               // stopped or restarted
      task = mTask;
    }
    task();
    // the next one an interval on; after a late one (a busy machine) no
    // burst to catch up, as a dispatch timer doesn't either
    next += interval;
    auto now = std::chrono::steady_clock::now();
    if (next < now)
      next = now + interval;
  }
}


/*-- the UI thread -----------------------------------------------------------*/

namespace {
std::mutex gRunnerLock;
std::function<void(std::function<void()>)> gRunner;
}


void SetMainThreadRunner(std::function<void(std::function<void()>)> inRunner)
{
  std::lock_guard<std::mutex> lock(gRunnerLock);
  gRunner = std::move(inRunner);
}


void RunOnMainThread(std::function<void()> inTask)
{
  std::function<void(std::function<void()>)> runner;
  {
    std::lock_guard<std::mutex> lock(gRunnerLock);
    runner = gRunner;
  }
  if (runner)
    runner(std::move(inTask));
  else
    inTask();
}

} // namespace ncx
