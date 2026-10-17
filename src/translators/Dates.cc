/*
 File: Dates.cc

 Dates entries and iCalendar (the Cocoa app's DateToICal, DateFromICal;
 libical's part is a little iCalendar code of its own here).

 The soups (and an entry's viewStationery):
   Calendar          meetings ('meeting)
   Repeat Meetings   repeating meetings ('repeatingMeeting,
                     'exceptionMeeting)
   Calendar Notes    events of a day ('cribNote)
   Repeat Notes      repeating events of a day ('cribNote, repeatType)
 A meeting: mtgStartDate (Newton date), mtgDuration (minutes), mtgText,
 mtgAlarm (a date; repeating: minutes before), mtgLocation {company},
 mtgInvitees [{name}], notesData [paragraphs]; repeating: repeatType,
 mtgInfo, mtgStopDate.

 Newton dates have no time zone: they become "floating" iCalendar times
 (no Z), and imported ones are taken as local time (UTC ones converted;
 a TZID is ignored). Export: all of an app's into one "Newton <app>.ics".
 Import: VEVENTs, each into its soup by kind (timed or a day's, repeating
 or not); locations and invitees are left out, as the Cocoa app did (they
 are references to Names cards on the Newton).
 */

#include "translators/Translator.h"
#include "translators/ContentLines.h"
#include "base/Dates.h"
#include "base/NewtonStrings.h"

#include "RSSymbols.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>

namespace ncx {

namespace {

const int64_t kEndOfTime = 97829280;          // 12:00am 1/1/2090: the default mtgStopDate

// repeatType
enum { kDayOfWeek, kWeekInMonth, kDateInMonth, kDateInYear, kPeriod, kNever, k6Reserved, kWeekInYear };
// mtgInfo: days of the week, weeks in the month
enum
{
  kSunday = 0x800, kMonday = 0x400, kTuesday = 0x200, kWednesday = 0x100, kThursday = 0x080, kFriday = 0x040,
  kSaturday = 0x020, kEveryday = 0xFE0,
  kFirstWeek = 0x010, kSecondWeek = 0x008, kThirdWeek = 0x004, kFourthWeek = 0x002, kLastWeek = 0x001, kEveryWeek = 0x01F
};
const struct { int bit; const char * code; } kDays[] = {
  { kMonday, "MO" }, { kTuesday, "TU" }, { kWednesday, "WE" }, { kThursday, "TH" }, { kFriday, "FR" },
  { kSaturday, "SA" }, { kSunday, "SU" } };
const struct { int bit; int position; } kWeeks[] = {
  { kFirstWeek, 1 }, { kSecondWeek, 2 }, { kThirdWeek, 3 }, { kFourthWeek, 4 }, { kLastWeek, -1 } };

/*-- times -------------------------------------------------------------------*/

std::string ICalDateTime(int64_t inMinutes)
{
  CivilTime t = UTCTime(TimeFromNewton(inMinutes));
  char text[24];
  std::snprintf(text, sizeof(text), "%04d%02d%02dT%02d%02d%02d", t.year, t.month, t.day, t.hour, t.minute, t.second);
  return text;
}

std::string ICalDate(int64_t inMinutes)
{
  return ICalDateTime(inMinutes).substr(0, 8);
}

/** "20261018", "20261018T100000" (floating), "...Z" (UTC, made local):
    a Newton date, or -1. outDateOnly: it had no time. */
int64_t NewtonFromICal(const std::string & inText, bool * outDateOnly = nullptr)
{
  CivilTime t;
  if (std::sscanf(inText.c_str(), "%4d%2d%2d", &t.year, &t.month, &t.day) != 3)
    return -1;
  bool dateOnly = inText.size() < 15 || inText[8] != 'T';
  if (!dateOnly)
    std::sscanf(inText.c_str() + 9, "%2d%2d%2d", &t.hour, &t.minute, &t.second);
  if (outDateOnly)
    *outDateOnly = dateOnly;
  int64_t seconds = TimeFromUTC(t);
  if (!dateOnly && inText.back() == 'Z') {
    CivilTime local = LocalTime(seconds);
    seconds = TimeFromUTC(local);
  }
  return NewtonFromTime(seconds);
}

/** Minutes as "PT1H30M", "P1D", "-PT15M". */
std::string ICalDuration(int64_t inMinutes)
{
  std::string text = inMinutes < 0 ? "-P" : "P";
  int64_t m = std::llabs(inMinutes);
  if (m == 0)
    return "PT0M";
  if (m % 1440 == 0)
    return text + std::to_string(m / 1440) + "D";
  if (m >= 1440) {
    text += std::to_string(m / 1440) + "D";
    m %= 1440;
  }
  text += "T";
  if (m >= 60)
    text += std::to_string(m / 60) + "H";
  if (m % 60)
    text += std::to_string(m % 60) + "M";
  return text;
}

/** "-P1DT2H30M15S", "P2W": minutes (seconds rounded down). */
int64_t MinutesFromICalDuration(const std::string & inText)
{
  int64_t sign = 1, seconds = 0, number = 0;
  for (char c : inText) {
    if (c == '-')
      sign = -1;
    else if (c >= '0' && c <= '9')
      number = number * 10 + (c - '0');
    else {
      switch (c) {
        case 'W': seconds += number * 7 * 86400; break;
        case 'D': seconds += number * 86400; break;
        case 'H': seconds += number * 3600; break;
        case 'M': seconds += number * 60; break;
        case 'S': seconds += number; break;
        default: break;
      }
      number = 0;
    }
  }
  return sign * seconds / 60;
}

/*-- iCalendar components ----------------------------------------------------*/

struct Component
{
  std::string name;                       // VCALENDAR, VEVENT, VALARM, ...
  std::vector<ContentLine> properties;
  std::vector<Component> children;

  const ContentLine * property(const char * inName) const
  {
    for (const ContentLine & line : properties)
      if (line.name == inName)
        return &line;
    return nullptr;
  }
  std::string text(const char * inName) const
  {
    const ContentLine * line = property(inName);
    return line ? UnescapeText(line->value) : std::string();
  }
  std::string format() const
  {
    std::string out = FormatContentLine(ContentLine("BEGIN", name));
    for (const ContentLine & line : properties)
      out += FormatContentLine(line);
    for (const Component & child : children)
      out += child.format();
    return out + FormatContentLine(ContentLine("END", name));
  }
};

/** The components of an iCalendar text (nested by BEGIN/END). */
std::vector<Component> ParseComponents(const std::string & inText)
{
  std::vector<Component> roots;
  std::vector<Component> stack;
  for (ContentLine & line : ParseContentLines(inText)) {
    if (line.name == "BEGIN") {
      Component c;
      c.name = Upper(line.value);
      stack.push_back(std::move(c));
    } else if (line.name == "END") {
      if (stack.empty())
        continue;
      Component done = std::move(stack.back());
      stack.pop_back();
      if (stack.empty())
        roots.push_back(std::move(done));
      else
        stack.back().children.push_back(std::move(done));
    } else if (!stack.empty()) {
      stack.back().properties.push_back(std::move(line));
    }
  }
  return roots;
}

/*-- export ------------------------------------------------------------------*/

/** RRULE BYDAY: the days of mtgInfo, with the weeks of the month if not
    all of them ("2MO", "-1FR"). */
std::string ByDay(int inInfo)
{
  std::string days;
  int weeks = inInfo & kEveryWeek;
  for (const auto & day : kDays) {
    if (!(inInfo & day.bit))
      continue;
    if (weeks == 0 || weeks == kEveryWeek) {
      days += (days.empty() ? "" : ",") + std::string(day.code);
      continue;
    }
    for (const auto & week : kWeeks)
      if (weeks & week.bit)
        days += (days.empty() ? "" : ",") + std::to_string(week.position) + day.code;
  }
  return days;
}


class DateToICal : public Exporter
{
public:
  void begin(const TranslatorContext & inContext) override
  {
    Exporter::begin(inContext);
    mCalendar.name = "VCALENDAR";
    mCalendar.properties.emplace_back("PRODID", "-//Newton Research//NONSGML NCX//EN");
    mCalendar.properties.emplace_back("VERSION", "2.0");
  }

  std::filesystem::path exportEntry(RefArg inEntry) override
  {
    RefVar stationery(GetFrameSlot(inEntry, SYMA(viewStationery)));
    bool meeting = EQRef(stationery, MakeSymbol("meeting"));
    bool repeating = EQRef(stationery, MakeSymbol("repeatingMeeting")) || EQRef(stationery, MakeSymbol("exceptionMeeting"));
    bool note = EQRef(stationery, MakeSymbol("cribNote"));
    if (!meeting && !repeating && !note)
      return std::filesystem::path();

    Component event;
    event.name = "VEVENT";
    RefVar start(GetFrameSlot(inEntry, MakeSymbol("mtgStartDate")));
    std::string text = StringSlot(inEntry, "mtgText");
    RefVar id(GetFrameSlot(inEntry, SYMA(_uniqueId)));
    event.properties.emplace_back("UID", "ncx-" + (ISINT(start) ? std::to_string(RINT(start)) : std::string("0")) + "-"
                                           + (ISINT(id) ? std::to_string(RINT(id)) : std::to_string(++mCount))
                                           + "@newton");
    event.properties.emplace_back("DTSTAMP", ICalDateTime(NewtonFromTime(Now())) + "Z");
    if (ISINT(start)) {
      if (note)
        event.properties.push_back(ContentLine("DTSTART", ICalDate(RINT(start))).addParam("VALUE", "DATE"));
      else
        event.properties.emplace_back("DTSTART", ICalDateTime(RINT(start)));
    }
    RefVar duration(GetFrameSlot(inEntry, MakeSymbol("mtgDuration")));
    if (!note && ISINT(duration))
      event.properties.emplace_back("DURATION", ICalDuration(RINT(duration)));
    if (!text.empty())
      event.properties.emplace_back("SUMMARY", EscapeText(text));
    repeat(inEntry, event);

    RefVar alarm(GetFrameSlot(inEntry, MakeSymbol("mtgAlarm")));
    if (ISINT(alarm) && !note) {
      Component valarm;
      valarm.name = "VALARM";
      valarm.properties.emplace_back("ACTION", "DISPLAY");
      if (repeating)        // minutes before
        valarm.properties.emplace_back("TRIGGER", ICalDuration(-RINT(alarm)));
      else                  // when
        valarm.properties.push_back(ContentLine("TRIGGER", ICalDateTime(RINT(alarm))).addParam("VALUE", "DATE-TIME"));
      valarm.properties.emplace_back("DESCRIPTION", EscapeText(text));
      event.children.push_back(valarm);
    }

    RefVar location(GetFrameSlot(inEntry, MakeSymbol("mtgLocation")));
    std::string place = StringSlot(location, "company");
    if (!place.empty())
      event.properties.emplace_back("LOCATION", EscapeText(place));
    RefVar invitees(GetFrameSlot(inEntry, MakeSymbol("mtgInvitees")));
    for (ArrayIndex i = 0, n = IsArray(invitees) ? Length(invitees) : 0; i < n; ++i) {
      std::string who = FullName(RefVar(GetFrameSlot(RefVar(GetArraySlot(invitees, i)), SYMA(name))));
      if (!who.empty())
        event.properties.push_back(ContentLine("ATTENDEE", "invalid:nomail").addParam("CN", who));
    }
    std::string description = ParagraphsText(RefVar(GetFrameSlot(inEntry, MakeSymbol("notesData"))));
    if (!description.empty())
      event.properties.emplace_back("DESCRIPTION", EscapeText(description));

    mCalendar.children.push_back(event);
    return std::filesystem::path();
  }

  std::filesystem::path end() override
  {
    if (mCalendar.children.empty())
      return std::filesystem::path();
    return write(mCalendar.format(), "Newton " + mContext.appName, "ics");
  }

private:
  void repeat(RefArg inEntry, Component & ioEvent)
  {
    RefVar type(GetFrameSlot(inEntry, MakeSymbol("repeatType")));
    RefVar info(GetFrameSlot(inEntry, MakeSymbol("mtgInfo")));
    if (!ISINT(type) || !ISINT(info))
      return;
    int mtgInfo = (int)(RINT(info) & 0x00FFFFFF);
    std::string rule;
    switch (RINT(type)) {
      case kDayOfWeek:
        rule = (mtgInfo & kEveryday) == kEveryday ? "FREQ=DAILY" : "FREQ=WEEKLY;BYDAY=" + ByDay(mtgInfo);
        break;
      case kWeekInMonth:
        rule = "FREQ=MONTHLY;BYDAY=" + ByDay(mtgInfo);
        break;
      case kDateInMonth:
        rule = "FREQ=MONTHLY;BYMONTHDAY=" + std::to_string(mtgInfo);
        break;
      case kDateInYear:
        rule = "FREQ=YEARLY;BYMONTH=" + std::to_string(mtgInfo >> 8) + ";BYMONTHDAY=" + std::to_string(mtgInfo & 0x1F);
        break;
      case kPeriod:
        rule = "FREQ=DAILY;INTERVAL=" + std::to_string(mtgInfo & 0xFF);
        break;
      case kWeekInYear:
        rule = "FREQ=YEARLY;BYMONTH=" + std::to_string(mtgInfo >> 12) + ";BYDAY=" + ByDay(mtgInfo & 0xFFF);
        break;
      default:
        return;
    }
    RefVar stop(GetFrameSlot(inEntry, MakeSymbol("mtgStopDate")));
    if (ISINT(stop) && RINT(stop) < kEndOfTime)
      rule += ";UNTIL=" + ICalDateTime(RINT(stop));
    ioEvent.properties.emplace_back("RRULE", rule);
  }

  Component mCalendar;
  int64_t mCount = 0;
};

/*-- import ------------------------------------------------------------------*/

int DayBit(const std::string & inCode)
{
  for (const auto & day : kDays)
    if (inCode == day.code)
      return day.bit;
  return 0;
}

int WeekBit(int inPosition)
{
  if (inPosition < 0)
    return kLastWeek;
  for (const auto & week : kWeeks)
    if (week.position == inPosition)
      return week.bit;
  return kEveryWeek;
}


class DateFromICal : public Importer
{
public:
  bool begin(const std::filesystem::path & inFile, const TranslatorContext & inContext) override
  {
    Importer::begin(inFile, inContext);
    std::ifstream in(inFile, std::ios::binary);
    if (!in) {
      mWhy = "can't read the file";
      return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (Component & calendar : ParseComponents(text))
      for (Component & child : calendar.children)
        if (child.name == "VEVENT")
          mEvents.push_back(std::move(child));
    return true;
  }

  Ref next(std::string & ioSoup) override
  {
    while (mNext < mEvents.size()) {
      const Component & event = mEvents[mNext++];
      const ContentLine * start = event.property("DTSTART");
      if (!start)
        continue;
      bool dateOnly = false;
      int64_t startDate = NewtonFromICal(start->value, &dateOnly);
      if (startDate < 0)
        continue;
      bool repeating = event.property("RRULE") != nullptr;
      ioSoup = dateOnly ? (repeating ? "Repeat Notes" : "Calendar Notes") : (repeating ? "Repeat Meetings" : "Calendar");
      return makeEntry(event, startDate, dateOnly, repeating);
    }
    return NILREF;
  }

private:
  Ref makeEntry(const Component & inEvent, int64_t inStart, bool inDateOnly, bool inRepeating)
  {
    RefVar entry(AllocateFrame());
    SetFrameSlot(entry, SYMA(class), MakeSymbol("meeting"));
    SetFrameSlot(entry, SYMA(viewStationery),
                 MakeSymbol(inDateOnly ? "cribNote" : inRepeating ? "repeatingMeeting" : "meeting"));
    SetFrameSlot(entry, MakeSymbol("mtgStartDate"), MAKEINT(inStart));
    int64_t duration = 0;
    if (!inDateOnly) {
      if (const ContentLine * d = inEvent.property("DURATION"))
        duration = MinutesFromICalDuration(d->value);
      else if (const ContentLine * end = inEvent.property("DTEND")) {
        int64_t endDate = NewtonFromICal(end->value);
        if (endDate > inStart)
          duration = endDate - inStart;
      }
    }
    SetFrameSlot(entry, MakeSymbol("mtgDuration"), MAKEINT(duration));
    SetFrameSlot(entry, MakeSymbol("mtgText"), RefVar(MakeStringFromUTF8(inEvent.text("SUMMARY"))));
    if (inRepeating)
      repeat(inEvent, inStart, entry);
    alarm(inEvent, inStart, inRepeating, entry);
    std::string description = inEvent.text("DESCRIPTION");
    if (!description.empty() && !inRepeating) {
      RefVar notes(MakeArray(1));
      SetArraySlot(notes, 0, RefVar(MakeParagraph(description, 5, 5, 315, 505, mContext.userFont)));
      SetFrameSlot(entry, MakeSymbol("notesData"), notes);
    }
    return entry;
  }

  void alarm(const Component & inEvent, int64_t inStart, bool inRepeating, RefArg ioEntry)
  {
    for (const Component & child : inEvent.children) {
      if (child.name != "VALARM")
        continue;
      // (macOS Calendar adds ACTION:NONE alarms to every event: not alarms)
      std::string action = Upper(child.text("ACTION"));
      const ContentLine * trigger = child.property("TRIGGER");
      if (!trigger || (action != "DISPLAY" && action != "AUDIO" && action != "EMAIL"))
        continue;
      bool absolute = Upper(trigger->param("VALUE")) == "DATE-TIME";
      int64_t when = absolute ? NewtonFromICal(trigger->value) : inStart + MinutesFromICalDuration(trigger->value);
      if (inRepeating)
        SetFrameSlot(ioEntry, MakeSymbol("mtgAlarm"), MAKEINT(inStart - when));       // minutes before
      else
        SetFrameSlot(ioEntry, MakeSymbol("mtgAlarm"), MAKEINT(when));
      return;
    }
  }

  void repeat(const Component & inEvent, int64_t inStart, RefArg ioEntry)
  {
    // FREQ=...;INTERVAL=...;BYDAY=...;BYMONTHDAY=...;BYMONTH=...;BYSETPOS=...;UNTIL=...
    std::string freq, byDay, until;
    int interval = 1, byMonthDay = 0, byMonth = 0, bySetPos = 0;
    for (const std::string & part : SplitValue(inEvent.property("RRULE")->value, ';')) {
      size_t equal = part.find('=');
      std::string key = Upper(part.substr(0, equal)), value = equal == std::string::npos ? "" : part.substr(equal + 1);
      if (key == "FREQ")
        freq = Upper(value);
      else if (key == "INTERVAL")
        interval = std::atoi(value.c_str());
      else if (key == "BYDAY")
        byDay = Upper(value);
      else if (key == "BYMONTHDAY")
        byMonthDay = std::atoi(value.c_str());
      else if (key == "BYMONTH")
        byMonth = std::atoi(value.c_str());
      else if (key == "BYSETPOS")
        bySetPos = std::atoi(value.c_str());
      else if (key == "UNTIL")
        until = value;
    }
    // the days of BYDAY, and the week in the month of the first
    int days = 0, week = bySetPos ? WeekBit(bySetPos) : kEveryWeek;
    for (const std::string & day : SplitValue(byDay, ',')) {
      if (day.size() < 2)
        continue;
      days |= DayBit(day.substr(day.size() - 2));
      if (day.size() > 2)
        week = WeekBit(std::atoi(day.substr(0, day.size() - 2).c_str()));
    }
    CivilTime start = UTCTime(TimeFromNewton(inStart));
    int repeatType = kNever, info = 0;
    if (freq == "DAILY") {
      if (interval <= 1) {
        repeatType = kDayOfWeek;
        info = kEveryday | kEveryWeek;
      } else {
        repeatType = kPeriod;
        info = interval | (int)((inStart / 1440) << 8);
      }
    } else if (freq == "WEEKLY") {
      repeatType = kDayOfWeek;
      info = kEveryWeek | (days ? days : kDays[(start.weekday + 6) % 7].bit);
    } else if (freq == "MONTHLY") {
      if (days && interval == 12) {
        repeatType = kWeekInYear;
        info = (start.month << 12) | days | week;
      } else if (days) {
        repeatType = kWeekInMonth;
        info = days | week;
      } else {
        repeatType = kDateInMonth;
        info = byMonthDay ? byMonthDay : start.day;
      }
    } else if (freq == "YEARLY") {
      int month = byMonth ? byMonth : start.month;
      if (days) {
        repeatType = kWeekInYear;
        info = (month << 12) | days | week;
      } else {
        repeatType = kDateInYear;
        info = (month << 8) | (byMonthDay ? byMonthDay : start.day);
      }
    }
    int64_t stop = until.empty() ? -1 : NewtonFromICal(until);
    SetFrameSlot(ioEntry, MakeSymbol("repeatType"), MAKEINT(repeatType));
    SetFrameSlot(ioEntry, MakeSymbol("mtgInfo"), MAKEINT(info));
    SetFrameSlot(ioEntry, MakeSymbol("mtgStopDate"), MAKEINT(stop >= 0 ? stop : kEndOfTime));
  }

  std::vector<Component> mEvents;
  size_t mNext = 0;
};

} // namespace


std::unique_ptr<Exporter> MakeDateToICal() { return std::make_unique<DateToICal>(); }
std::unique_ptr<Importer> MakeDateFromICal() { return std::make_unique<DateFromICal>(); }

} // namespace ncx
