/*
 File: Log.h

 One logger for NCX (PORTING.md 2.5), replacing NSLog, REPprintf for
 messages, and the Cocoa app's MINIMUM_LOG/FULL_LOG/DEVELOPER_LOG. Its
 levels are the app's:

   0  none
   1  minimum: protocol exchanges, progress
   2  full: backup and restore entries
   3  developer: anything else (hidden from the UI)

 Lines go to stderr (unless turned off), to a log file if there is one (the
 preferences LogToFile, LogLevel; with a time stamp), and to a sink if one is
 set (a log window, tests). Any thread may log.
 */

#ifndef NCX_BASE_LOG_H
#define NCX_BASE_LOG_H

#include <filesystem>
#include <functional>
#include <string>

namespace ncx {

enum LogLevel { kLogNone = 0, kLogMinimum = 1, kLogFull = 2, kLogDeveloper = 3 };

/** Which messages are logged: those of this level and below (default 0:
    none). */
void SetLogLevel(int inLevel);
int LogLevelSetting();
bool LogEnabled(int inLevel);

/** Also into this file (appended); an empty path closes it. False if it
    can't be opened. */
bool SetLogFile(const std::filesystem::path & inFile);

/** Lines to stderr too (default: yes). */
void SetLogToStderr(bool inOn);

/** Every line also to this (nullptr: none). Called on the thread that logs. */
void SetLogSink(std::function<void(int inLevel, const std::string & inLine)> inSink);

/** A message, printf style, if its level is enabled: one line (a newline
    is added if it has none). */
void Log(int inLevel, const char * inFormat, ...)
#if defined(__GNUC__)
  __attribute__((format(printf, 2, 3)))
#endif
  ;

} // namespace ncx

#define NCX_LOG_MINIMUM(...)   do { if (ncx::LogEnabled(ncx::kLogMinimum)) ncx::Log(ncx::kLogMinimum, __VA_ARGS__); } while (0)
#define NCX_LOG_FULL(...)      do { if (ncx::LogEnabled(ncx::kLogFull)) ncx::Log(ncx::kLogFull, __VA_ARGS__); } while (0)
#define NCX_LOG_DEVELOPER(...) do { if (ncx::LogEnabled(ncx::kLogDeveloper)) ncx::Log(ncx::kLogDeveloper, __VA_ARGS__); } while (0)

#endif // NCX_BASE_LOG_H
