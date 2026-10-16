/*
 File: Dates.cc

 Times, their conversions, and their formatting (Dates.h).
 */

#include "base/Dates.h"

#include <cmath>
#include <cstdio>
#include <ctime>

namespace ncx {

namespace {

const char * const kMonths[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

int64_t FloorDiv(int64_t a, int64_t b)
{
  int64_t q = a / b;
  return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

// days since 1970-01-01 of a Gregorian date, and back (Howard Hinnant's
// algorithms: any year, no time zone, no table)
int64_t DaysFromCivil(int64_t y, int m, int d)
{
  y -= m <= 2;
  int64_t era = FloorDiv(y, 400);
  int64_t yoe = y - era * 400;
  int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void CivilFromDays(int64_t z, int & outY, int & outM, int & outD)
{
  z += 719468;
  int64_t era = FloorDiv(z, 146097);
  int64_t doe = z - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  outD = int(doy - (153 * mp + 2) / 5 + 1);
  outM = int(mp < 10 ? mp + 3 : mp - 9);
  outY = int(yoe + era * 400 + (outM <= 2));
}

CivilTime FromTm(const std::tm & inTm)
{
  CivilTime t;
  t.year = inTm.tm_year + 1900;
  t.month = inTm.tm_mon + 1;
  t.day = inTm.tm_mday;
  t.hour = inTm.tm_hour;
  t.minute = inTm.tm_min;
  t.second = inTm.tm_sec;
  t.weekday = inTm.tm_wday;
  return t;
}

} // namespace


int64_t Now()
{
  return (int64_t)std::time(nullptr);
}


int64_t TimeFromNewton(int64_t inMinutes)
{
  return (inMinutes - kMinutes1904to1970) * 60;
}


int64_t NewtonFromTime(int64_t inSeconds)
{
  return FloorDiv(inSeconds, 60) + kMinutes1904to1970;
}


double CoreDataFromTime(int64_t inSeconds)
{
  return double(inSeconds - kSeconds1970to2001);
}


int64_t TimeFromCoreData(double inSeconds)
{
  return (int64_t)std::floor(inSeconds) + kSeconds1970to2001;
}


CivilTime UTCTime(int64_t inSeconds)
{
  CivilTime t;
  int64_t days = FloorDiv(inSeconds, 86400);
  int64_t rest = inSeconds - days * 86400;
  CivilFromDays(days, t.year, t.month, t.day);
  t.hour = int(rest / 3600);
  t.minute = int(rest / 60 % 60);
  t.second = int(rest % 60);
  int64_t weekday = (days + 4) % 7;                    // 1970-01-01 was a Thursday
  t.weekday = int(weekday < 0 ? weekday + 7 : weekday);
  return t;
}


CivilTime LocalTime(int64_t inSeconds)
{
  std::time_t t = (std::time_t)inSeconds;
  std::tm tm {};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  return FromTm(tm);
}


int64_t TimeFromUTC(const CivilTime & inTime)
{
  return DaysFromCivil(inTime.year, inTime.month, inTime.day) * 86400
       + inTime.hour * 3600 + inTime.minute * 60 + inTime.second;
}


int64_t TimeFromLocal(const CivilTime & inTime)
{
  std::tm tm {};
  tm.tm_year = inTime.year - 1900;
  tm.tm_mon = inTime.month - 1;
  tm.tm_mday = inTime.day;
  tm.tm_hour = inTime.hour;
  tm.tm_min = inTime.minute;
  tm.tm_sec = inTime.second;
  tm.tm_isdst = -1;           // the time zone's rules decide
  return (int64_t)std::mktime(&tm);
}


std::string FormatDate(int64_t inSeconds)
{
  CivilTime t = LocalTime(inSeconds);
  char text[32];
  std::snprintf(text, sizeof(text), "%s %d, %d", kMonths[t.month - 1], t.day, t.year);
  return text;
}


std::string FormatTime(int64_t inSeconds)
{
  CivilTime t = LocalTime(inSeconds);
  int hour12 = t.hour % 12 == 0 ? 12 : t.hour % 12;
  char text[16];
  std::snprintf(text, sizeof(text), "%d:%02d %s", hour12, t.minute, t.hour < 12 ? "AM" : "PM");
  return text;
}


std::string FormatDateTime(int64_t inSeconds)
{
  return FormatDate(inSeconds) + " at " + FormatTime(inSeconds);
}


std::string FormatRelativeDateTime(int64_t inSeconds, int64_t inNow)
{
  CivilTime then = LocalTime(inSeconds), now = LocalTime(inNow);
  int64_t days = DaysFromCivil(then.year, then.month, then.day) - DaysFromCivil(now.year, now.month, now.day);
  if (days == 0)
    return "Today at " + FormatTime(inSeconds);
  if (days == -1)
    return "Yesterday at " + FormatTime(inSeconds);
  if (days == 1)
    return "Tomorrow at " + FormatTime(inSeconds);
  return FormatDateTime(inSeconds);
}


std::string FormatForFileName(int64_t inSeconds)
{
  CivilTime t = LocalTime(inSeconds);
  char text[32];
  std::snprintf(text, sizeof(text), "%04d-%02d-%02d %02d.%02d", t.year, t.month, t.day, t.hour, t.minute);
  return text;
}

} // namespace ncx
