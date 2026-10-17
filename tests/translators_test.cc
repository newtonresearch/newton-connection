/*
 File: translators_test.cc

 Unit tests of NCX's translators (PORTING.md Phase 6): vCard and iCalendar
 lines, Names and vCards, Dates and iCalendar, Notes from text, the
 built-in ones (packages, text, NewtonScript), and exporting and importing
 the entries of a .newtondevice file. One section per ctest:

   ncx_translators_test <section> [<fixture file>]
       lines, names, dates, notes, builtin, transfer <fixture>, or all

 Prints "ok <what>" or "FAIL <what>" per check; exit 0 if all are ok. Files
 go to a folder in the temp folder, removed afterwards.
 */

#include "base/Core.h"
#include "base/Dates.h"
#include "base/NewtonCall.h"
#include "base/NewtonStrings.h"
#include "model/DeviceFile.h"
#include "model/NewtonModel.h"
#include "platform/AppFolders.h"
#include "translators/ContentLines.h"
#include "translators/Transfer.h"
#include "translators/Translator.h"

#include "RSSymbols.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ncx;

namespace {

int gFailed = 0;
std::string gFixture;

void Check(bool inOk, const std::string & inWhat)
{
  std::printf("%s %s\n", inOk ? "ok     " : "FAIL   ", inWhat.c_str());
  if (!inOk)
    ++gFailed;
}

struct TempFolder
{
  fs::path path;
  explicit TempFolder(const std::string & inName)
  {
    path = fs::temp_directory_path() / ("ncx-translators-" + inName + "-" + std::to_string(Now() % 100000));
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~TempFolder()
  {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

std::string ReadFile(const fs::path & inFile)
{
  std::ifstream in(inFile, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void WriteFile(const fs::path & inFile, const std::string & inData)
{
  std::ofstream out(inFile, std::ios::binary);
  out << inData;
}

size_t Count(const std::string & inText, const std::string & inWhat)
{
  size_t n = 0;
  for (size_t at = inText.find(inWhat); at != std::string::npos; at = inText.find(inWhat, at + 1))
    ++n;
  return n;
}

Ref Str(const std::string & inText) { return MakeStringFromUTF8(inText); }
std::string Text(RefArg inFrame, const char * inSlot) { return StringSlot(inFrame, inSlot); }
int64_t Int(RefArg inFrame, const char * inSlot)
{
  RefVar v(GetFrameSlot(inFrame, MakeSymbol(inSlot)));
  return ISINT(v) ? RINT(v) : -1;
}

/** The entries an importer makes of a file, with their soups. */
std::vector<std::pair<std::string, RefStruct>> Import(const std::string & inApp, const fs::path & inFile)
{
  std::vector<std::pair<std::string, RefStruct>> entries;
  const ImportFormat * format = FindImportFormat(inApp, inFile);
  if (!format)
    return entries;
  std::unique_ptr<Importer> importer = format->make();
  TranslatorContext context;
  context.appName = inApp;
  if (!importer->begin(inFile, context))
    return entries;
  for (;;) {
    std::string soup = format->soup;
    RefVar entry(importer->next(soup));
    if (ISNIL(entry))
      break;
    entries.emplace_back(soup, RefStruct(entry));
  }
  importer->end();
  return entries;
}

/** Entries exported into inFolder as inApp's; the files. */
std::vector<fs::path> Export(const fs::path & inFolder, const std::string & inApp, const std::vector<Ref> & inEntries)
{
  TranslatorContext context;
  context.folder = inFolder;
  context.appName = inApp;
  ExportSession session(context);
  for (Ref entry : inEntries)
    session.add(RefVar(entry));
  return session.end();
}

int64_t NewtonDate(int y, int m, int d, int hour = 0, int minute = 0)
{
  CivilTime t;
  t.year = y;
  t.month = m;
  t.day = d;
  t.hour = hour;
  t.minute = minute;
  return NewtonFromTime(TimeFromUTC(t));
}

/*-- lines -------------------------------------------------------------------*/

void TestLines()
{
  std::vector<ContentLine> lines = ParseContentLines(
    "BEGIN:VCARD\r\nitem1.TEL;type=WORK;TYPE=\"VOICE,PREF\":555\r\nNOTE:a long\r\n  line\\, folded\\nhere\r\nEMAIL;INTERNET:x@y\r\nbroken\r\n");
  Check(lines.size() == 4 && lines[1].group == "item1" && lines[1].name == "TEL" && lines[1].hasType("work")
        && lines[1].hasType("pref") && lines[1].value == "555", "lines: group, name, parameters, quoted lists");
  Check(lines[2].value == "a long line\\, folded\\nhere" && UnescapeText(lines[2].value) == "a long line, folded\nhere",
        "lines: unfolded, unescaped");
  Check(lines[3].hasType("INTERNET"), "lines: vCard 2.1's bare parameters");
  Check(EscapeText("a;b,c\\d\r\ne") == "a\\;b\\,c\\\\d\\ne" && UnescapeText(EscapeText("a;b,c\\d\ne")) == "a;b,c\\d\ne",
        "lines: escaping both ways");
  std::vector<std::string> parts = SplitValue("Last;First\\;x;;Dr.;", ';');
  Check(parts.size() == 5 && parts[1] == "First;x" && parts[3] == "Dr.", "lines: structured values");
  // folded at 75 octets, never inside a character
  std::string longText;
  for (int i = 0; i < 60; ++i)
    longText += "\xC3\xA4";           // a umlaut, two octets each
  std::string folded = FormatContentLine(ContentLine("NOTE", longText));
  bool short75 = true, whole = true;
  size_t start = 0;
  for (size_t end; (end = folded.find("\r\n", start)) != std::string::npos; start = end + 2) {
    short75 = short75 && end - start <= 75;
    if (end > start)
      whole = whole && ((unsigned char)folded[end - 1] & 0xE0) != 0xC0;     // no lead byte at a line's end
  }
  std::vector<ContentLine> back = ParseContentLines(folded);
  Check(short75 && whole && back.size() == 1 && back[0].value == longText, "lines: folded at 75 octets, between characters");
}

/*-- names -------------------------------------------------------------------*/

void TestNames()
{
  TempFolder folder("names");
  RefVar card(AllocateFrame());
  SetFrameSlot(card, SYMA(class), SYMA(person));
  RefVar name(AllocateFrame());
  SetFrameSlot(name, SYMA(class), SYMA(person));
  SetFrameSlot(name, SYMA(first), RefVar(Str("Ada")));
  SetFrameSlot(name, MakeSymbol("last"), RefVar(Str("Lovelace")));
  SetFrameSlot(name, MakeSymbol("honorific"), RefVar(Str("Countess")));
  SetFrameSlot(card, SYMA(name), name);
  SetFrameSlot(card, SYMA(company), RefVar(Str("Analytical Engines; Ltd.")));
  SetFrameSlot(card, SYMA(title), RefVar(Str("Programmer")));
  SetFrameSlot(card, SYMA(address), RefVar(Str("12 St. James's Square")));
  SetFrameSlot(card, MakeSymbol("address2"), RefVar(Str("Second floor")));
  SetFrameSlot(card, MakeSymbol("city"), RefVar(Str("London")));
  SetFrameSlot(card, MakeSymbol("postal_code"), RefVar(Str("SW1Y 4JH")));
  SetFrameSlot(card, MakeSymbol("country"), RefVar(Str("UK")));
  RefVar phones(MakeArray(0));
  RefVar phone(Str("+44 20 1234"));
  SetClass(phone, MakeSymbol("workPhone"));
  AddArraySlot(phones, phone);
  phone = Str("+44 77 5678");
  SetClass(phone, MakeSymbol("mobilePhone"));
  AddArraySlot(phones, phone);
  phone = Str("+44 20 9999");
  SetClass(phone, MakeSymbol("homefaxPhone"));
  AddArraySlot(phones, phone);
  SetFrameSlot(card, MakeSymbol("phones"), phones);
  RefVar pagers(MakeArray(1));
  RefVar pager(AllocateFrame());
  SetFrameSlot(pager, MakeSymbol("pagerNum"), RefVar(Str("123")));
  SetArraySlot(pagers, 0, pager);
  SetFrameSlot(card, MakeSymbol("pagers"), pagers);
  SetFrameSlot(card, MakeSymbol("email"), RefVar(Str("ada@engine.example")));
  RefVar emails(MakeArray(1));
  RefVar email(AllocateFrame());
  SetFrameSlot(email, MakeSymbol("email"), RefVar(Str("countess@example.org")));
  SetArraySlot(emails, 0, email);
  SetFrameSlot(card, MakeSymbol("emailAddrs"), emails);
  RefVar urls(MakeArray(1));
  SetArraySlot(urls, 0, RefVar(Str("http://engine.example")));
  SetFrameSlot(card, MakeSymbol("URLs"), urls);
  SetFrameSlot(card, MakeSymbol("bday"), MAKEINT(NewtonDate(1915, 12, 10)));
  SetFrameSlot(card, MakeSymbol("anniversary"), MAKEINT(NewtonDate(1935, 7, 8)));
  RefVar relations(MakeArray(1));
  RefVar other(AllocateFrame());
  SetFrameSlot(other, SYMA(class), SYMA(person));
  SetFrameSlot(other, SYMA(title), RefVar(Str("Friend")));
  SetFrameSlot(other, SYMA(first), RefVar(Str("Charles")));
  SetFrameSlot(other, MakeSymbol("last"), RefVar(Str("Babbage")));
  SetArraySlot(relations, 0, other);
  SetFrameSlot(card, MakeSymbol("names"), relations);
  RefVar notes(MakeArray(1));
  SetArraySlot(notes, 0, RefVar(MakeParagraph("Notes on the engine,\nline two", 0, 0, 10, 10, 0)));
  SetFrameSlot(card, MakeSymbol("notes"), notes);

  RefVar company(AllocateFrame());
  SetFrameSlot(company, SYMA(class), SYMA(company));
  SetFrameSlot(company, SYMA(company), RefVar(Str("Conglomerated Credit")));
  SetFrameSlot(company, SYMA(name), RefVar(AllocateFrame()));
  SetFrameSlot(company, MakeSymbol("city"), RefVar(Str("Brisbane")));
  SetFrameSlot(company, MakeSymbol("phones"), RefVar(MakeArray(0)));
  SetFrameSlot(company, SYMA(version), MAKEINT(2));

  std::vector<fs::path> files = Export(folder.path, "Names", { card, company });
  Check(files.size() == 1 && files[0].filename() == "Newton Names.vcf", "names: one .vcf for all the cards");
  if (std::getenv("NCX_SHOW"))
    for (const fs::path & f : files)
      std::printf("%s:\n%s\n", UTF8FromPath(f).c_str(), ReadFile(f).c_str());
  std::string vcf = files.empty() ? std::string() : ReadFile(files[0]);
  Check(Count(vcf, "BEGIN:VCARD\r\n") == 2 && vcf.find("VERSION:3.0\r\n") != std::string::npos
        && vcf.find("N:Lovelace;Ada;;Countess;\r\n") != std::string::npos && vcf.find("FN:Ada Lovelace\r\n") != std::string::npos
        && vcf.find("ORG:Analytical Engines\\; Ltd.;\r\n") != std::string::npos,
        "names: vCard 3.0, N, FN, ORG escaped");
  Check(vcf.find("ADR;TYPE=HOME:;;12 St. James's Square\\nSecond floor;London;;SW1Y 4JH;UK\r\n") != std::string::npos
        && vcf.find("TEL;TYPE=\"WORK,VOICE\":+44 20 1234\r\n") != std::string::npos
        && vcf.find("TEL;TYPE=\"HOME,FAX\":+44 20 9999\r\n") != std::string::npos
        && vcf.find("TEL;TYPE=PAGER:123\r\n") != std::string::npos && vcf.find("BDAY:1915-12-10\r\n") != std::string::npos,
        "names: ADR, TEL by class, the pager, BDAY");
  Check(vcf.find("item1.X-ABDATE:1935-07-08\r\nitem1.X-ABLabel:_$!<Anniversary>!$_\r\n") != std::string::npos
        && vcf.find("item2.X-ABRELATEDNAMES:Charles Babbage\r\nitem2.X-ABLabel:_$!<Friend>!$_\r\n") != std::string::npos
        && vcf.find("X-ABShowAs:COMPANY") != std::string::npos,
        "names: the anniversary and relations as the Address Book writes them; a company");

  auto entries = Import("Names", files.empty() ? fs::path() : files[0]);
  Check(entries.size() == 2 && entries[0].first == "Names", "names: two cards back, into Names");
  if (entries.size() != 2)
    return;
  RefVar ada(entries[0].second);
  RefVar adaName(GetFrameSlot(ada, SYMA(name)));
  Check(Text(adaName, "first") == "Ada" && Text(adaName, "last") == "Lovelace" && Text(adaName, "honorific") == "Countess"
        && Text(ada, "company") == "Analytical Engines; Ltd." && Text(ada, "title") == "Programmer"
        && Text(ada, "sorton") == "Lovelace Ada", "names: the name, company, job, sorted on");
  Check(Text(ada, "address") == "12 St. James's Square" && Text(ada, "address2") == "Second floor"
        && Text(ada, "city") == "London" && Text(ada, "postal_code") == "SW1Y 4JH" && Text(ada, "country") == "UK",
        "names: the address");
  RefVar backPhones(GetFrameSlot(ada, MakeSymbol("phones")));
  bool phoneClasses = IsArray(backPhones) && Length(backPhones) == 3
                      && EQRef(ClassOf(RefVar(GetArraySlot(backPhones, 0))), MakeSymbol("workPhone"))
                      && EQRef(ClassOf(RefVar(GetArraySlot(backPhones, 1))), MakeSymbol("mobilePhone"))
                      && EQRef(ClassOf(RefVar(GetArraySlot(backPhones, 2))), MakeSymbol("homefaxPhone"))
                      && UTF8FromString(RefVar(GetArraySlot(backPhones, 0))) == "+44 20 1234";
  RefVar backPagers(GetFrameSlot(ada, MakeSymbol("pagers")));
  Check(phoneClasses && IsArray(backPagers) && Length(backPagers) == 1, "names: phones by class, the pager");
  RefVar backEmails(GetFrameSlot(ada, MakeSymbol("emailAddrs")));
  RefVar backUrls(GetFrameSlot(ada, MakeSymbol("URLs")));
  Check(Text(ada, "email") == "ada@engine.example" && IsArray(backEmails) && Length(backEmails) == 1
        && Text(RefVar(GetArraySlot(backEmails, 0)), "email") == "countess@example.org" && IsArray(backUrls)
        && Length(backUrls) == 1, "names: e-mail addresses, URLs");
  Check(Int(ada, "bday") == NewtonDate(1915, 12, 10) && Int(ada, "anniversary") == NewtonDate(1935, 7, 8),
        "names: birthday and anniversary");
  RefVar backRelations(GetFrameSlot(ada, MakeSymbol("names")));
  RefVar charles(IsArray(backRelations) && Length(backRelations) == 1 ? GetArraySlot(backRelations, 0) : (Ref)NILREF);
  Check(Text(charles, "first") == "Charles" && Text(charles, "last") == "Babbage" && Text(charles, "title") == "Friend",
        "names: the relation");
  RefVar backNotes(GetFrameSlot(ada, MakeSymbol("notes")));
  Check(ParagraphsText(backNotes) == "Notes on the engine,\nline two", "names: the notes");
  RefVar credit(entries[1].second);
  Check(EQRef(GetFrameSlot(credit, SYMA(class)), SYMA(company)) && Text(credit, "company") == "Conglomerated Credit"
        && Text(credit, "sorton") == "Conglomerated Credit" && Text(credit, "city") == "Brisbane",
        "names: a company is a company");

  // a vCard 2.1 from elsewhere: bare parameters, only FN
  WriteFile(folder.path / "other.vcf", "BEGIN:VCARD\nVERSION:2.1\nFN:Mary Somerville\nTEL;CELL:555-1\nEND:VCARD\n");
  entries = Import("Names", folder.path / "other.vcf");
  RefVar mary(entries.size() == 1 ? (Ref)entries[0].second : (Ref)NILREF);
  RefVar maryPhones(GetFrameSlot(mary, MakeSymbol("phones")));
  Check(Text(RefVar(GetFrameSlot(mary, SYMA(name))), "first") == "Mary"
        && Text(RefVar(GetFrameSlot(mary, SYMA(name))), "last") == "Somerville" && IsArray(maryPhones)
        && EQRef(ClassOf(RefVar(GetArraySlot(maryPhones, 0))), MakeSymbol("mobilePhone")),
        "names: a vCard 2.1 with only a formatted name");
}

/*-- dates -------------------------------------------------------------------*/

Ref Meeting(const char * inStationery, int64_t inStart, int inDuration, const char * inText)
{
  RefVar entry(AllocateFrame());
  SetFrameSlot(entry, SYMA(class), MakeSymbol("meeting"));
  SetFrameSlot(entry, SYMA(viewStationery), MakeSymbol(inStationery));
  SetFrameSlot(entry, MakeSymbol("mtgStartDate"), MAKEINT(inStart));
  SetFrameSlot(entry, MakeSymbol("mtgDuration"), MAKEINT(inDuration));
  SetFrameSlot(entry, MakeSymbol("mtgText"), RefVar(Str(inText)));
  return entry;
}

void TestDates()
{
  TempFolder folder("dates");
  int64_t start = NewtonDate(2026, 10, 18, 10, 30);
  RefVar meeting(Meeting("meeting", start, 90, "Engine review"));
  SetFrameSlot(meeting, MakeSymbol("mtgAlarm"), MAKEINT(start - 15));
  RefVar location(AllocateFrame());
  SetFrameSlot(location, SYMA(company), RefVar(Str("The Royal Society")));
  SetFrameSlot(meeting, MakeSymbol("mtgLocation"), location);
  RefVar invitees(MakeArray(1));
  RefVar invitee(AllocateFrame());
  RefVar inviteeName(AllocateFrame());
  SetFrameSlot(inviteeName, SYMA(first), RefVar(Str("Charles")));
  SetFrameSlot(inviteeName, MakeSymbol("last"), RefVar(Str("Babbage")));
  SetFrameSlot(invitee, SYMA(name), inviteeName);
  SetArraySlot(invitees, 0, invitee);
  SetFrameSlot(meeting, MakeSymbol("mtgInvitees"), invitees);
  RefVar notes(MakeArray(1));
  SetArraySlot(notes, 0, RefVar(MakeParagraph("Bring the cards", 0, 0, 10, 10, 0)));
  SetFrameSlot(meeting, MakeSymbol("notesData"), notes);

  RefVar weekly(Meeting("repeatingMeeting", NewtonDate(2026, 10, 19, 9), 60, "Stand-up"));
  SetFrameSlot(weekly, MakeSymbol("repeatType"), MAKEINT(0));                     // kDayOfWeek
  SetFrameSlot(weekly, MakeSymbol("mtgInfo"), MAKEINT(0x400 | 0x100 | 0x01F));    // Monday, Wednesday, every week
  SetFrameSlot(weekly, MakeSymbol("mtgStopDate"), MAKEINT(NewtonDate(2026, 12, 31)));
  SetFrameSlot(weekly, MakeSymbol("mtgAlarm"), MAKEINT(10));                      // 10 minutes before
  RefVar monthly(Meeting("repeatingMeeting", NewtonDate(2026, 10, 13, 14), 30, "Committee"));
  SetFrameSlot(monthly, MakeSymbol("repeatType"), MAKEINT(1));                    // kWeekInMonth
  SetFrameSlot(monthly, MakeSymbol("mtgInfo"), MAKEINT(0x200 | 0x008));           // second Tuesday
  SetFrameSlot(monthly, MakeSymbol("mtgStopDate"), MAKEINT(97829280));
  RefVar birthday(Meeting("cribNote", NewtonDate(2026, 12, 10), 0, "Ada's birthday"));
  SetFrameSlot(birthday, MakeSymbol("repeatType"), MAKEINT(3));                   // kDateInYear
  SetFrameSlot(birthday, MakeSymbol("mtgInfo"), MAKEINT(12 << 8 | 10));
  RefVar holiday(Meeting("cribNote", NewtonDate(2026, 11, 1), 0, "Holiday"));

  std::vector<fs::path> files = Export(folder.path, "Dates", { meeting, weekly, monthly, birthday, holiday });
  Check(files.size() == 1 && files[0].filename() == "Newton Dates.ics", "dates: one .ics for all");
  std::string ics = files.empty() ? std::string() : ReadFile(files[0]);
  Check(ics.compare(0, 17, "BEGIN:VCALENDAR\r\n") == 0 && Count(ics, "BEGIN:VEVENT") == 5 && Count(ics, "UID:") == 5
        && ics.find("VERSION:2.0") != std::string::npos, "dates: a calendar of five events, each with a UID");
  Check(ics.find("DTSTART:20261018T103000\r\nDURATION:PT1H30M\r\nSUMMARY:Engine review\r\n") != std::string::npos
        && ics.find("TRIGGER;VALUE=DATE-TIME:20261018T101500") != std::string::npos
        && ics.find("LOCATION:The Royal Society") != std::string::npos
        && ics.find("ATTENDEE;CN=Charles Babbage:invalid:nomail") != std::string::npos
        && ics.find("DESCRIPTION:Bring the cards") != std::string::npos,
        "dates: a meeting (floating time, duration, alarm, location, invitee, notes)");
  Check(ics.find("RRULE:FREQ=WEEKLY;BYDAY=MO,WE;UNTIL=20261231T000000") != std::string::npos
        && ics.find("TRIGGER:-PT10M") != std::string::npos && ics.find("RRULE:FREQ=MONTHLY;BYDAY=2TU\r\n") != std::string::npos
        && ics.find("DTSTART;VALUE=DATE:20261210\r\n") != std::string::npos
        && ics.find("RRULE:FREQ=YEARLY;BYMONTH=12;BYMONTHDAY=10") != std::string::npos,
        "dates: repeat rules, an alarm before, a day's event");

  auto entries = Import("Dates", files.empty() ? fs::path() : files[0]);
  Check(entries.size() == 5 && entries[0].first == "Calendar" && entries[1].first == "Repeat Meetings"
        && entries[2].first == "Repeat Meetings" && entries[3].first == "Repeat Notes" && entries[4].first == "Calendar Notes",
        "dates: back, each into its soup");
  if (entries.size() != 5)
    return;
  RefVar m(entries[0].second);
  Check(Int(m, "mtgStartDate") == start && Int(m, "mtgDuration") == 90 && Text(m, "mtgText") == "Engine review"
        && Int(m, "mtgAlarm") == start - 15 && EQRef(GetFrameSlot(m, SYMA(viewStationery)), MakeSymbol("meeting"))
        && ParagraphsText(RefVar(GetFrameSlot(m, MakeSymbol("notesData")))) == "Bring the cards",
        "dates: the meeting");
  RefVar w(entries[1].second);
  Check(Int(w, "repeatType") == 0 && Int(w, "mtgInfo") == (0x400 | 0x100 | 0x01F) && Int(w, "mtgAlarm") == 10
        && Int(w, "mtgStopDate") == NewtonDate(2026, 12, 31)
        && EQRef(GetFrameSlot(w, SYMA(viewStationery)), MakeSymbol("repeatingMeeting")), "dates: every Monday and Wednesday");
  RefVar mo(entries[2].second);
  Check(Int(mo, "repeatType") == 1 && Int(mo, "mtgInfo") == (0x200 | 0x008) && Int(mo, "mtgStopDate") == 97829280,
        "dates: the second Tuesday, no end");
  RefVar b(entries[3].second);
  Check(Int(b, "repeatType") == 3 && Int(b, "mtgInfo") == (12 << 8 | 10) && Int(b, "mtgDuration") == 0
        && EQRef(GetFrameSlot(b, SYMA(viewStationery)), MakeSymbol("cribNote")), "dates: a yearly day");

  // from elsewhere: DTEND, UTC, no-action alarms, an alarm before
  WriteFile(folder.path / "other.ics",
            "BEGIN:VCALENDAR\nVERSION:2.0\nBEGIN:VEVENT\nDTSTART:20261020T080000\nDTEND:20261020T091500\n"
            "SUMMARY:Lecture\nBEGIN:VALARM\nACTION:NONE\nTRIGGER;VALUE=DATE-TIME:19760401T005545Z\nEND:VALARM\n"
            "BEGIN:VALARM\nACTION:DISPLAY\nTRIGGER:-PT30M\nEND:VALARM\nEND:VEVENT\nBEGIN:VEVENT\n"
            "DTSTART:20261021T120000Z\nDURATION:PT1H\nSUMMARY:UTC\nEND:VEVENT\nBEGIN:VTODO\nSUMMARY:x\nEND:VTODO\nEND:VCALENDAR\n");
  entries = Import("Dates", folder.path / "other.ics");
  int64_t lecture = NewtonDate(2026, 10, 20, 8);
  CivilTime utcNoon;
  utcNoon.year = 2026;
  utcNoon.month = 10;
  utcNoon.day = 21;
  utcNoon.hour = 12;
  int64_t localNoon = NewtonFromTime(TimeFromUTC(LocalTime(TimeFromUTC(utcNoon))));
  Check(entries.size() == 2 && Int(entries[0].second, "mtgDuration") == 75 && Int(entries[0].second, "mtgAlarm") == lecture - 30
        && Int(entries[1].second, "mtgStartDate") == localNoon, "dates: DTEND, alarms without an action left out, UTC made local");
}

/*-- notes -------------------------------------------------------------------*/

void TestNotes()
{
  TempFolder folder("notes");
  std::string text;
  for (int i = 0; text.size() < 10000; ++i)
    text += "word" + std::to_string(i) + (i % 10 == 9 ? "\n" : " ");
  WriteFile(folder.path / "Letter.txt", text);
  auto entries = Import("Notes", folder.path / "Letter.txt");
  std::string joined;
  bool small = true, paperroll = true;
  for (auto & e : entries) {
    RefVar data(GetFrameSlot(e.second, SYMA(data)));
    std::string part = ParagraphsText(data);
    small = small && part.size() <= 4096;
    joined += part;
    paperroll = paperroll && EQRef(GetFrameSlot(e.second, SYMA(class)), MakeSymbol("paperroll")) && e.first == "Notes";
  }
  Check(entries.size() == 3 && small && joined == text && paperroll, "notes: 10 KB of text in three notes, between words");
  Check(entries.size() == 3 && Text(entries[0].second, "title") == "Letter" && Text(entries[2].second, "title") == "Letter (3)",
        "notes: titled after the file");
  WriteFile(folder.path / "latin.txt", "caf\xE9");
  entries = Import("Notes", folder.path / "latin.txt");
  Check(entries.size() == 1 && ParagraphsText(RefVar(GetFrameSlot(entries[0].second, SYMA(data)))) == "caf\xC3\xA9",
        "notes: a Latin-1 file");
  WriteFile(folder.path / "wide.txt", std::string("\xFF\xFE" "a\0\xE9\0", 6));
  entries = Import("Notes", folder.path / "wide.txt");
  Check(entries.size() == 1 && ParagraphsText(RefVar(GetFrameSlot(entries[0].second, SYMA(data)))) == "a\xC3\xA9",
        "notes: a UTF-16 file");
  Check(ImportAppsFor("x.TXT") == std::vector<std::string>({ "Notes" }) && ImportAppsFor("x.vcf") == std::vector<std::string>({ "Names" })
        && ImportAppsFor("x.doc").empty(), "notes: which app imports what");
}

/*-- builtin -----------------------------------------------------------------*/

void TestBuiltin()
{
  TempFolder folder("builtin");
  RefVar package(AllocateFrame());
  RefVar bytes(AllocateBinary(MakeSymbol("package"), 16));
  std::memcpy(BinaryData(bytes), "package0 a test", 16);
  SetFrameSlot(package, MakeSymbol("pkgRef"), bytes);
  SetFrameSlot(package, MakeSymbol("packageName"), RefVar(Str("Tetris:Games")));
  RefVar text(AllocateFrame());
  SetFrameSlot(text, SYMA(text), RefVar(Str("Hello\rNewton")));
  SetFrameSlot(text, SYMA(title), RefVar(Str("Greeting")));
  RefVar note(AllocateFrame());
  SetFrameSlot(note, SYMA(class), MakeSymbol("paperroll"));
  SetFrameSlot(note, MakeSymbol("height"), MAKEINT(52));
  Check(ExportClassOf(package) == "*package*" && ExportClassOf(text) == "text" && ExportClassOf(note) == "paperroll",
        "builtin: the class an entry is exported by");

  std::vector<fs::path> files = Export(folder.path, "Extras", { package, text, note, package });
  std::vector<std::string> names;
  for (const fs::path & f : files)
    names.push_back(UTF8FromPath(f.filename()));
  Check(names == std::vector<std::string>({ "Tetris-Games.newtonpkg", "Greeting.text", "Tetris-Games-2.newtonpkg",
                                            "Newton Extras.text" }),
        "builtin: a file each, names a file system takes, none overwritten, the rest printed");
  Check(names.size() == 4 && ReadFile(files[0]) == std::string("package0 a test", 16) && ReadFile(files[1]) == "Hello\nNewton",
        "builtin: the package's bytes, the text");
  std::string printed = names.size() == 4 ? ReadFile(files[3]) : std::string();
  Check(printed.find("paperroll") != std::string::npos && printed.find("52") != std::string::npos,
        "builtin: other entries printed as NewtonScript");
}

/*-- transfer ----------------------------------------------------------------*/

void TestTransfer()
{
  TempFolder folder("transfer");
  fs::path path = folder.path / "0000-4E65-7774-6F6E.newtondevice";
  fs::copy_file(PathFromUTF8(gFixture), path);
  std::string error;
  std::unique_ptr<DeviceFile> file = DeviceFile::Open(path, DeviceFile::kReadWrite, error);
  Check(file != nullptr, "transfer: the fixture " + error);
  if (!file)
    return;
  std::optional<AppRecord> names = file->findApp("Names");
  ExportResult exported = names ? ExportApp(*file, *names, 0, folder.path / "out") : ExportResult();
  std::string vcf = exported.files.size() == 2 ? ReadFile(exported.files[0]) : std::string();
  std::string printed = exported.files.size() == 2 ? ReadFile(exported.files[1]) : std::string();
  Check(exported.entries == 8 && Count(vcf, "BEGIN:VCARD") == 6 && vcf.find("FN:Royce Walthrop") != std::string::npos
        && vcf.find("ORG:Conglomerated Credit;") != std::string::npos
        && exported.files[0].filename() == "Newton Names.vcf", "transfer: Einstein's six cards as a .vcf");
  Check(Count(printed, "class: 'worksite") == 2 && exported.files[1].filename() == "Newton Names.text",
        "transfer: its two worksites printed (no translator for them)");

  StoreRecord internal = SortedStores(*file).at(0);
  ImportResult imported = exported.files.size() == 2 ? ImportFile(*file, internal, "Names", exported.files[0]) : ImportResult();
  std::optional<SoupRecord> soup = file->findSoup(internal.pk, "Names");
  size_t madeHere = 0;
  for (const EntryRecord & entry : file->entries(soup ? soup->pk : 0))
    madeHere += entry.uniqueId >= kImportIdBase;
  Check(imported.entries == 6 && imported.soups["Names"] == 6 && imported.error.empty() && madeHere == 6,
        "transfer: and back, as entries made on the desktop");
  ImportResult wrong = ImportFile(*file, internal, "Names", folder.path / "x.ics");
  Check(!wrong.error.empty() && wrong.entries == 0, "transfer: an app that doesn't take the file says so: " + wrong.error);
}

} // namespace


int main(int argc, char * argv[])
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::string section = argc > 1 ? argv[1] : "all";
  gFixture = argc > 2 ? argv[2] : "";
  InitCore();
  struct { const char * name; void (*test)(); } sections[] = {
    { "lines", TestLines }, { "names", TestNames }, { "dates", TestDates }, { "notes", TestNotes },
    { "builtin", TestBuiltin }, { "transfer", TestTransfer } };
  bool found = false;
  for (auto & s : sections)
    if (section == s.name || (section == "all" && (std::string(s.name) != "transfer" || !gFixture.empty()))) {
      try {
        CallNewton([&] { s.test(); });
      } catch (const std::exception & e) {
        Check(false, std::string(s.name) + ": " + e.what());
      }
      found = true;
    }
  if (!found) {
    std::fprintf(stderr, "no section \"%s\"\n", section.c_str());
    return 2;
  }
  std::printf("%s\n", gFailed ? "FAILED" : "all ok");
  return gFailed ? 1 : 0;
}
