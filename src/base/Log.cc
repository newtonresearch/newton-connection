/*
 File: Log.cc

 The logger (Log.h).
 */

#include "base/Log.h"
#include "base/Dates.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <vector>

namespace ncx {

namespace {

std::atomic<int> gLevel { kLogNone };
std::atomic<bool> gToStderr { true };
std::mutex gLock;
std::ofstream gFile;          // under gLock
std::function<void(int, const std::string &)> gSink;    // under gLock

} // namespace


void SetLogLevel(int inLevel)
{
  gLevel = inLevel < kLogNone ? kLogNone : (inLevel > kLogDeveloper ? kLogDeveloper : inLevel);
}


int LogLevelSetting()
{
  return gLevel;
}


bool LogEnabled(int inLevel)
{
  return inLevel > kLogNone && inLevel <= gLevel;
}


bool SetLogFile(const std::filesystem::path & inFile)
{
  std::lock_guard<std::mutex> lock(gLock);
  if (gFile.is_open())
    gFile.close();
  if (inFile.empty())
    return true;
  gFile.open(inFile, std::ios::out | std::ios::app | std::ios::binary);
  return gFile.is_open();
}


void SetLogToStderr(bool inOn)
{
  gToStderr = inOn;
}


void SetLogSink(std::function<void(int, const std::string &)> inSink)
{
  std::lock_guard<std::mutex> lock(gLock);
  gSink = std::move(inSink);
}


void Log(int inLevel, const char * inFormat, ...)
{
  if (!LogEnabled(inLevel))
    return;
  va_list args;
  va_start(args, inFormat);
  va_list copy;
  va_copy(copy, args);
  int size = std::vsnprintf(nullptr, 0, inFormat, copy);
  va_end(copy);
  std::string line;
  if (size > 0) {
    std::vector<char> buffer(size_t(size) + 1);
    std::vsnprintf(buffer.data(), buffer.size(), inFormat, args);
    line.assign(buffer.data(), size_t(size));
  }
  va_end(args);
  if (line.empty() || line.back() != '\n')
    line += '\n';

  std::lock_guard<std::mutex> lock(gLock);
  if (gToStderr) {
    std::fputs(line.c_str(), stderr);
    std::fflush(stderr);
  }
  if (gFile.is_open()) {
    CivilTime t = LocalTime(Now());
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d ", t.year, t.month, t.day, t.hour, t.minute, t.second);
    gFile << stamp << line;
    gFile.flush();
  }
  if (gSink)
    gSink(inLevel, line);
}

} // namespace ncx
