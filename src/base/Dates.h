/*
 File: Dates.h

 Times in NCX (PORTING.md 2.2): seconds since 1970 (UTC, as time_t, but 64
 bits everywhere), the Newton's minutes since 1904, and Core Data's seconds
 since 2001 (the .newtondevice files). The conversions are plain arithmetic,
 as the Cocoa app's were (kMinutes1904to1970): a Newton time is read as if
 its clock ran on UTC.

 Formatting replaces the Cocoa app's NSDateFormatters (medium date style,
 short time style, "Today"/"Yesterday" for the last backup), in English, in
 the local time zone.
 */

#ifndef NCX_BASE_DATES_H
#define NCX_BASE_DATES_H

#include <cstdint>
#include <string>

namespace ncx {

const int64_t kMinutes1904to1970 = 34714080;
const int64_t kSeconds1970to2001 = 978307200;

/** Seconds since 1970 now. */
int64_t Now();

/** Newton time (minutes since 1904) and seconds since 1970. */
int64_t TimeFromNewton(int64_t inMinutes);
int64_t NewtonFromTime(int64_t inSeconds);        // whole minutes, rounded down

/** Core Data's time (seconds since 2001, a double) and seconds since 1970. */
double CoreDataFromTime(int64_t inSeconds);
int64_t TimeFromCoreData(double inSeconds);        // whole seconds, rounded down

/** A date and time on the calendar (Gregorian). */
struct CivilTime
{
  int year = 1970;
  int month = 1;      // 1 ... 12
  int day = 1;        // 1 ... 31
  int hour = 0;
  int minute = 0;
  int second = 0;
  int weekday = 4;    // 0 Sunday ... 6 Saturday (output only)
};

CivilTime UTCTime(int64_t inSeconds);
CivilTime LocalTime(int64_t inSeconds);
int64_t TimeFromUTC(const CivilTime & inTime);       // weekday ignored
int64_t TimeFromLocal(const CivilTime & inTime);     // weekday ignored

/** "Oct 16, 2026" (medium date style) */
std::string FormatDate(int64_t inSeconds);
/** "3:04 PM" (short time style) */
std::string FormatTime(int64_t inSeconds);
/** "Oct 16, 2026 at 3:04 PM" */
std::string FormatDateTime(int64_t inSeconds);
/** "Today at 3:04 PM", "Yesterday at ...", "Tomorrow at ...", else as
    FormatDateTime (relative to inNow; calendar days, local time). */
std::string FormatRelativeDateTime(int64_t inSeconds, int64_t inNow);
/** "2026-10-16 15.04", for file names (local time; no ':', which Windows
    and the Finder don't allow). */
std::string FormatForFileName(int64_t inSeconds);

} // namespace ncx

#endif // NCX_BASE_DATES_H
