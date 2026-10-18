/*
 File: base_test.cc

 Unit tests of NCX's base layer (PORTING.md Phase 2): strings, Newton
 strings, dates, app folders, preferences, logging, threads, progress,
 observers. One section per ctest:

   ncx_base_test <section>     strings, newton, dates, folders, serial, prefs,
                               log, threads, progress, observers, or all

 Prints "ok <what>" or "FAIL <what>" per check; exit 0 if all are ok.
 */

#include "base/Core.h"
#include "base/Dates.h"
#include "base/Log.h"
#include "base/NewtonStrings.h"
#include "base/Observers.h"
#include "base/Prefs.h"
#include "base/Progress.h"
#include "base/Strings.h"
#include "base/Threads.h"
#include "platform/AppFolders.h"
#include "platform/SerialPorts.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <initializer_list>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace ncx;

namespace {

int gFailed = 0;

void Check(bool inOk, const std::string & inWhat)
{
  std::printf("%s %s\n", inOk ? "ok     " : "FAIL   ", inWhat.c_str());
  if (!inOk)
    ++gFailed;
}

void SetEnv(const char * inName, const std::string & inValue)
{
#if defined(_WIN32)
  // as UTF-16: _putenv_s would take the bytes in the ANSI code page
  std::u16string name = UTF16FromUTF8(inName), value = UTF16FromUTF8(inValue);
  _wputenv_s(reinterpret_cast<const wchar_t *>(name.c_str()), reinterpret_cast<const wchar_t *>(value.c_str()));
#else
  if (inValue.empty())
    unsetenv(inName);
  else
    setenv(inName, inValue.c_str(), 1);
#endif
}

void Sleep(int inMilliseconds)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(inMilliseconds));
}

// A UI thread for tests: RunOnMainThread queues, Drain() runs what is queued.
std::mutex gMainLock;
std::deque<std::function<void()>> gMainQueue;

void UseTestMainThread()
{
  SetMainThreadRunner([](std::function<void()> inTask) {
    std::lock_guard<std::mutex> lock(gMainLock);
    gMainQueue.push_back(std::move(inTask));
  });
}

int Drain()
{
  int count = 0;
  for (;;) {
    std::function<void()> task;
    {
      std::lock_guard<std::mutex> lock(gMainLock);
      if (gMainQueue.empty())
        return count;
      task = std::move(gMainQueue.front());
      gMainQueue.pop_front();
    }
    task();
    ++count;
  }
}


/*-- strings -----------------------------------------------------------------*/

/** inText == inExpected (char16_t values); the text in hex if not. Sources
    stay ASCII: MSVC reads them in the ANSI code page. */
void CheckUTF16(const std::u16string & inText, std::initializer_list<char16_t> inExpected, const std::string & inWhat)
{
  bool ok = inText == std::u16string(inExpected);
  std::string hex;
  if (!ok) {
    char code[8];
    for (char16_t c : inText) {
      std::snprintf(code, sizeof(code), " %04X", (unsigned)c);
      hex += code;
    }
    hex = " (got" + hex + ")";
  }
  Check(ok, inWhat + hex);
}

void TestStrings()
{
  const uint16_t chars[] = { 'G', 'r', 0xFC, 0xDF, 'e', ' ', 0xD83D, 0xDE00 };   // "Grusse" with u-umlaut and sharp s, a smiley
  std::string utf8 = UTF8FromUTF16(chars, 8);
  Check(utf8 == "Gr\xC3\xBC\xC3\x9F" "e \xF0\x9F\x98\x80", "UTF8FromUTF16: Latin-1 and a surrogate pair");
  std::u16string back = UTF16FromUTF8(utf8);
  Check(back.size() == 8 && back[6] == 0xD83D && back[7] == 0xDE00 && back[2] == 0xFC, "UTF16FromUTF8: and back");
  const uint16_t lone[] = { 'a', 0xD800, 'b', 0xDC00 };
  Check(UTF8FromUTF16(lone, 4) == "a\xEF\xBF\xBD" "b\xEF\xBF\xBD", "UTF8FromUTF16: lone surrogates become U+FFFD");
  const char16_t bad = 0xFFFD;
  CheckUTF16(UTF16FromUTF8("a\xFF" "b"), { u'a', bad, u'b' }, "UTF16FromUTF8: a byte that isn't UTF-8 becomes U+FFFD");
  CheckUTF16(UTF16FromUTF8("\xC0\xAF"), { bad, bad }, "UTF16FromUTF8: an overlong form isn't UTF-8");
  CheckUTF16(UTF16FromUTF8("\xE2\x82"), { bad, bad }, "UTF16FromUTF8: a cut-off sequence");

  Check(UTF8FromMacRoman("\x80\xA5\xDB\xF0 ok") == "\xC3\x84\xE2\x80\xA2\xE2\x82\xAC\xEF\xA3\xBF ok",
        "UTF8FromMacRoman: \xC3\x84, bullet, euro, the Apple logo");

  Check(ReplaceAll("say \"hi\" \"you\"", "\"", "\"\"") == "say \"\"hi\"\" \"\"you\"\"", "ReplaceAll (Works' CSV quotes)");
  Check(ReplaceAll("aaa", "aa", "b") == "ba" && ReplaceAll("abc", "", "x") == "abc", "ReplaceAll: overlaps, empty pattern");
  Check(ReplaceControlCharacters("a\tb\nc\x7F" "d\xC2\x85" "e\xC3\xA9", " ") == "a b c d e\xC3\xA9",
        "ReplaceControlCharacters (Works' summaries), U+0085 too, \xC3\xA9 not");

  Check(NewtonLineEnds("a\nb\r\nc\rd") == "a\rb\rc\rd", "NewtonLineEnds: LF, CR LF, CR -> CR");
  Check(DesktopLineEnds("a\rb\r\nc\nd") == "a\nb\nc\nd", "DesktopLineEnds: CR, CR LF, LF -> LF");
}


/*-- Newton strings ----------------------------------------------------------*/

void TestNewtonStrings()
{
  InitCore();
  RefVar str(MakeStringFromUTF8("Gr\xC3\xBC\xC3\x9F" "e\nNewton"));
  Check(IsString(str) && Length(str) == (12 + 1) * sizeof(UniChar), "MakeStringFromUTF8: a string of 12 UniChars and a 0");
  Check(UTF8FromString(str) == "Gr\xC3\xBC\xC3\x9F" "e\rNewton", "UTF8FromString: back, the line end a CR (as the Newton's)");
  Check(UTF8FromStringLF(str) == "Gr\xC3\xBC\xC3\x9F" "e\nNewton", "UTF8FromStringLF: the line end an LF");
  Check(UTF8FromString(MAKEINT(42)).empty() && UTF8FromSymbol(MAKEINT(42)).empty(), "not a string, not a symbol: \"\"");
  Check(UTF8FromSymbol(MakeSymbol("viewBounds")) == "viewBounds", "UTF8FromSymbol");
}


/*-- dates -------------------------------------------------------------------*/

void TestDates()
{
  Check(NewtonFromTime(0) == 34714080 && TimeFromNewton(34714080) == 0, "Newton time: 1970 is minute 34714080 since 1904");
  Check(NewtonFromTime(-1) == 34714079 && NewtonFromTime(59) == 34714080, "NewtonFromTime rounds down");
  Check(TimeFromCoreData(0.0) == 978307200 && CoreDataFromTime(978307260) == 60.0 && TimeFromCoreData(-0.5) == 978307199,
        "Core Data time: seconds since 2001");

  CivilTime t = UTCTime(0);
  Check(t.year == 1970 && t.month == 1 && t.day == 1 && t.hour == 0 && t.weekday == 4, "UTCTime(0): Thursday 1970-01-01");
  t = UTCTime(951782400 + 3723);
  Check(t.year == 2000 && t.month == 2 && t.day == 29 && t.hour == 1 && t.minute == 2 && t.second == 3 && t.weekday == 2,
        "UTCTime: Tuesday 2000-02-29 01:02:03");
  t = UTCTime(TimeFromNewton(0));
  Check(t.year == 1904 && t.month == 1 && t.day == 1 && t.weekday == 5, "the Newton's epoch: Friday 1904-01-01");
  CivilTime c; c.year = 2026; c.month = 10; c.day = 16; c.hour = 15; c.minute = 4; c.second = 5;
  Check(UTCTime(TimeFromUTC(c)).day == 16 && TimeFromUTC(c) % 60 == 5, "TimeFromUTC and back");

  int64_t when = TimeFromLocal(c);
  CivilTime local = LocalTime(when);
  Check(local.year == 2026 && local.month == 10 && local.day == 16 && local.hour == 15 && local.minute == 4,
        "TimeFromLocal and LocalTime agree");
  Check(FormatDate(when) == "Oct 16, 2026", "FormatDate: \"" + FormatDate(when) + "\"");
  Check(FormatTime(when) == "3:04 PM", "FormatTime: \"" + FormatTime(when) + "\"");
  Check(FormatDateTime(when) == "Oct 16, 2026 at 3:04 PM", "FormatDateTime");
  Check(FormatForFileName(when) == "2026-10-16 15.04", "FormatForFileName: \"" + FormatForFileName(when) + "\"");
  CivilTime midnight = c; midnight.hour = 0; midnight.minute = 0;
  CivilTime noon = c; noon.hour = 12; noon.minute = 30;
  Check(FormatTime(TimeFromLocal(midnight)) == "12:00 AM" && FormatTime(TimeFromLocal(noon)) == "12:30 PM",
        "FormatTime: midnight and noon");

  CivilTime morning = c; morning.hour = 9;
  CivilTime yesterday = c; yesterday.day = 15; yesterday.hour = 23;
  CivilTime tomorrow = c; tomorrow.day = 17; tomorrow.hour = 1;
  CivilTime earlier = c; earlier.day = 13;
  Check(FormatRelativeDateTime(TimeFromLocal(morning), when) == "Today at 9:04 AM", "FormatRelativeDateTime: today");
  Check(FormatRelativeDateTime(TimeFromLocal(yesterday), when) == "Yesterday at 11:04 PM", "FormatRelativeDateTime: yesterday");
  Check(FormatRelativeDateTime(TimeFromLocal(tomorrow), when) == "Tomorrow at 1:04 AM", "FormatRelativeDateTime: tomorrow");
  Check(FormatRelativeDateTime(TimeFromLocal(earlier), when) == "Oct 13, 2026 at 3:04 PM", "FormatRelativeDateTime: earlier");
}


/*-- app folders -------------------------------------------------------------*/

void TestFolders()
{
  fs::path base = fs::temp_directory_path() / ("ncx-test-" + std::to_string(Now() % 100000));
  fs::path data = base / PathFromUTF8("Gr\xC3\xBC\xC3\x9F" "e");   // (a narrow literal is ANSI on Windows)
  fs::remove_all(base);
  SetEnv("NCX_DATA_DIR", UTF8FromPath(data));
  fs::path folder = AppDataFolder();
  Check(folder == data && fs::is_directory(folder), "AppDataFolder: NCX_DATA_DIR, made");
  Check(AppDataFile("Backup 1.newtondevice") == data / "Backup 1.newtondevice", "AppDataFile");
  SetEnv("NCX_LOG_FILE", UTF8FromPath(base / "logs" / "ncx.log"));
  fs::path log = LogFilePath();
  Check(log == base / "logs" / "ncx.log" && fs::is_directory(base / "logs"), "LogFilePath: NCX_LOG_FILE, its folder made");
  Check(UTF8FromPath(PathFromUTF8("Gr\xC3\xBC\xC3\x9F" "e")) == "Gr\xC3\xBC\xC3\x9F" "e", "UTF-8 paths and back");
  SetEnv("NCX_DATA_DIR", "");
  SetEnv("NCX_LOG_FILE", "");
  fs::remove_all(base);
}


/*-- serial ports ------------------------------------------------------------*/

void TestSerialPorts()
{
  std::vector<SerialPort> ports = ListSerialPorts();
  bool ok = true;
  for (const SerialPort & port : ports) {
    std::printf("        %s (%s)\n", port.path.c_str(), port.name.c_str());
#if defined(__APPLE__)
    ok = ok && port.path.compare(0, 8, "/dev/cu.") == 0;
#elif defined(_WIN32)
    ok = ok && port.path.compare(0, 3, "COM") == 0;
#else
    ok = ok && port.path.compare(0, 5, "/dev/") == 0;
#endif
  }
  Check(ok && std::is_sorted(ports.begin(), ports.end(), [](const SerialPort & a, const SerialPort & b) { return a.path < b.path; }),
        "ListSerialPorts: " + std::to_string(ports.size()) + " ports, paths as the system has them, sorted");
}


/*-- preferences -------------------------------------------------------------*/

void TestPrefs()
{
  std::unique_ptr<Prefs> prefs = Prefs::InMemory("ncx-base-test");
  Check(!prefs->has(pref::kPassword) && prefs->getString(pref::kPassword).empty() && !prefs->getBool(pref::kAutoBackup)
        && prefs->getInt(pref::kROMSize) == 0, "unset: the defaults");
  Check(prefs->tcpPort() == 3679 && prefs->baudRate() == 38400 && prefs->logLevel() == 0 && !prefs->logToFile(),
        "unset: the Cocoa app's fallbacks (3679, 38400)");
  prefs->setString(pref::kPassword, "Gr\xC3\xBC\xC3\x9F" "e");
  prefs->setBool(pref::kAutoBackup, true);
  prefs->setInt(pref::kTCPIPPort, 4000);
  prefs->setInt(pref::kSerialBaud, 57600);
  prefs->setDouble(pref::kUserFontSize, 12.5);
  prefs->setInt(pref::kLogLevel, 9);
  Check(prefs->getString(pref::kPassword) == "Gr\xC3\xBC\xC3\x9F" "e" && prefs->getBool(pref::kAutoBackup)
        && prefs->tcpPort() == 4000 && prefs->baudRate() == 57600 && prefs->getDouble(pref::kUserFontSize) == 12.5,
        "set and get: string (UTF-8), bool, ints, double");
  Check(prefs->logLevel() == 3, "logLevel: 0 ... 3");
  prefs->remove(pref::kPassword);
  Check(!prefs->has(pref::kPassword) && prefs->has(pref::kAutoBackup), "remove");
  std::thread other([&] { for (int i = 0; i < 1000; ++i) prefs->setInt(pref::kROMSize, i); });
  for (int i = 0; i < 1000; ++i)
    prefs->getInt(pref::kROMSize);
  other.join();
  Check(prefs->getInt(pref::kROMSize) == 999, "two threads at once");
}


/*-- logging -----------------------------------------------------------------*/

void TestLog()
{
  SetLogToStderr(false);
  std::vector<std::string> lines;
  SetLogSink([&](int, const std::string & inLine) { lines.push_back(inLine); });
  SetLogLevel(kLogNone);
  Log(kLogMinimum, "nothing");
  Check(lines.empty() && !LogEnabled(kLogMinimum), "level 0: nothing is logged");
  SetLogLevel(kLogFull);
  Log(kLogMinimum, "minimum %d", 1);
  NCX_LOG_FULL("full %s", "two");
  NCX_LOG_DEVELOPER("developer");
  Log(kLogMinimum, "with its own newline\n");
  Check(lines.size() == 3 && lines[0] == "minimum 1\n" && lines[1] == "full two\n" && lines[2] == "with its own newline\n",
        "level 2: minimum and full, not developer; one newline each");

  fs::path file = fs::temp_directory_path() / ("ncx-log-" + std::to_string(Now() % 100000) + ".log");
  fs::remove(file);
  Check(SetLogFile(file), "SetLogFile");
  Log(kLogMinimum, "into the file");
  SetLogFile(fs::path());
  std::ifstream in(file);
  std::stringstream text;
  text << in.rdbuf();
  in.close();
  std::string s = text.str();
  Check(s.size() == 20 + 14 && s.compare(20, 14, "into the file\n") == 0 && s[4] == '-' && s[13] == ':',
        "the file: a time stamp and the line");
  fs::remove(file);
  std::thread a([] { for (int i = 0; i < 200; ++i) Log(kLogMinimum, "a %d", i); });
  std::thread b([] { for (int i = 0; i < 200; ++i) Log(kLogMinimum, "b %d", i); });
  a.join();
  b.join();
  Check(lines.size() == 3 + 1 + 400, "two threads logging");
  SetLogSink(nullptr);
  SetLogLevel(kLogNone);
  SetLogToStderr(true);
}


/*-- threads -----------------------------------------------------------------*/

void TestThreads()
{
  std::vector<int> order;
  bool onQueue = false, inlineSync = false;
  {
    SerialQueue queue("test");
    for (int i = 0; i < 1000; ++i)
      queue.async([&order, i] { order.push_back(i); });
    queue.sync([&] { onQueue = queue.isCurrent(); queue.sync([&] { inlineSync = true; }); });
    Check(order.size() == 1000 && !queue.isCurrent(), "SerialQueue: sync waits for what is queued before it");
    bool inOrder = true;
    for (int i = 0; i < 1000; ++i)
      inOrder = inOrder && order[i] == i;
    Check(inOrder, "SerialQueue: in order");
    Check(onQueue && inlineSync, "SerialQueue: isCurrent on it; sync on it runs at once (no deadlock)");
    queue.async([&order] { Sleep(20); order.push_back(-1); });
  }   // the destructor runs what is left
  Check(order.size() == 1001 && order.back() == -1, "SerialQueue: stop() runs what is queued");
  SerialQueue stopped("stopped");
  stopped.stop();
  int ran = 0;
  stopped.async([&] { ++ran; });
  stopped.sync([&] { ran += 10; });
  Check(ran == 10, "SerialQueue: stopped, async is ignored, sync runs here");

  Semaphore semaphore;
  Check(!semaphore.waitFor(10), "Semaphore: none, the wait times out");
  std::thread giver([&] { Sleep(20); semaphore.signal(); });
  semaphore.wait();
  giver.join();
  semaphore.signal();
  Check(semaphore.waitFor(0), "Semaphore: signal, wait");

  std::atomic<int> ticks { 0 };
  Timer timer;
  // three runs, waited for up to 2 s (a busy CI machine runs it late), no
  // more than the time allows
  auto started = std::chrono::steady_clock::now();
  timer.start(0.02, [&] { ++ticks; });
  for (int i = 0; i < 200 && ticks < 3; ++i)
    Sleep(10);
  timer.stop();
  int stoppedAt = ticks;
  long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
  Sleep(60);
  Check(stoppedAt >= 3 && stoppedAt <= elapsed / 20 + 1 && ticks == stoppedAt && !timer.running(),
        "Timer: every 20 ms (" + std::to_string(stoppedAt) + " in " + std::to_string(elapsed) + " ms), none after stop()");
  ticks = 0;
  timer.start(0.01, [&] { if (++ticks == 3) timer.stop(); });
  for (int i = 0; i < 200 && ticks < 3; ++i)     // (a slow CI machine: up to 2 s)
    Sleep(10);
  Sleep(50);
  Check(ticks == 3, "Timer: stopped from its own task");

  SetMainThreadRunner(nullptr);
  bool here = false;
  RunOnMainThread([&] { here = true; });
  Check(here, "RunOnMainThread without a UI: at once");
  UseTestMainThread();
  bool later = false;
  std::thread worker([&] { RunOnMainThread([&] { later = true; }); });
  worker.join();
  Check(!later && Drain() == 1 && later, "RunOnMainThread with a UI: when the UI runs it");
  SetMainThreadRunner(nullptr);
}


/*-- progress ----------------------------------------------------------------*/

void TestProgress()
{
  Progress progress;
  Check(progress.fraction() == -1.0 && !progress.cancelled(), "Progress: total not known");
  progress.setTotal(200);
  progress.add(30);
  progress.add(20);
  Check(progress.completed() == 50 && progress.fraction() == 0.25, "Progress: 50 of 200");
  progress.setCompleted(500);
  Check(progress.fraction() == 1.0, "Progress: no more than all");
  progress.setDescription("Backing up Names");
  std::thread ui([&] { progress.cancel(); });
  ui.join();
  Check(progress.cancelled() && progress.description() == "Backing up Names", "Progress: cancelled from another thread");
}


/*-- observers ---------------------------------------------------------------*/

void TestObservers()
{
  UseTestMainThread();
  Observers observers;
  std::vector<Notification> got;
  int keyboard = 0;
  Observers::Token token = observers.add(NotificationKind::kOperation, [&](const Notification & n) { got.push_back(n); });
  observers.add(NotificationKind::kRequestKeyboard, [&](const Notification &) { ++keyboard; });
  int document = 0;
  std::thread dock([&] {
    Notification n;
    n.kind = NotificationKind::kOperation;
    n.document = &document;
    n.operation = 3;
    n.error = -28012;
    observers.post(n);
  });
  dock.join();
  Check(got.empty(), "Observers: not on the dock's thread");
  Drain();
  Check(got.size() == 1 && got[0].document == &document && got[0].operation == 3 && got[0].error == -28012 && keyboard == 0,
        "Observers: on the UI thread, with document, operation, error; only to its kind");
  Notification n;
  n.kind = NotificationKind::kOperation;
  observers.post(n);
  observers.remove(token);
  Drain();
  Check(got.size() == 1, "Observers: removed before it was delivered: not called");
  n.kind = NotificationKind::kRequestKeyboard;
  observers.post(n);
  Drain();
  Check(keyboard == 1, "Observers: another kind");
  SetMainThreadRunner(nullptr);
}

} // namespace


int main(int argc, char * argv[])
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::string section = argc > 1 ? argv[1] : "all";
  struct { const char * name; void (*test)(); } sections[] = {
    { "strings", TestStrings }, { "newton", TestNewtonStrings }, { "dates", TestDates },
    { "folders", TestFolders }, { "serial", TestSerialPorts }, { "prefs", TestPrefs }, { "log", TestLog },
    { "threads", TestThreads }, { "progress", TestProgress }, { "observers", TestObservers } };
  bool found = false;
  for (auto & s : sections)
    if (section == "all" || section == s.name) {
      s.test();
      found = true;
    }
  if (!found) {
    std::fprintf(stderr, "no section \"%s\"\n", section.c_str());
    return 2;
  }
  std::printf("%s\n", gFailed ? "FAILED" : "all ok");
  return gFailed ? 1 : 0;
}
