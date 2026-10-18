/*
 File: translators_test.cc

 Unit tests of NCX's translators (PORTING.md Phase 6): vCard and iCalendar
 lines, Names and vCards, Dates and iCalendar, Notes from text, the
 built-in ones (packages, text, NewtonScript), and exporting and importing
 the entries of a .newtondevice file. One section per ctest:

   ncx_translators_test <section> [<fixture file>]
       lines, names, dates, notes, builtin, graphics, transfer <fixture>,
       or all (graphics needs a display on Linux)

 Prints "ok <what>" or "FAIL <what>" per check; exit 0 if all are ok. Files
 go to a folder in the temp folder, removed afterwards.
 */

#include "base/Core.h"
#include "base/Dates.h"
#include "base/NewtonCall.h"
#include "base/NewtonStrings.h"
#include "base/Strings.h"
#include "comms/DockRefs.h"
#include "model/DeviceFile.h"
#include "model/NewtonModel.h"
#include "platform/AppFolders.h"
#include "translators/Canvas.h"
#include "translators/ContentLines.h"
#include "translators/NewtonText.h"
#include "translators/Render.h"
#include "translators/RichText.h"
#include "translators/Transfer.h"
#include "translators/Translator.h"

#include "RSSymbols.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <set>
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
std::vector<fs::path> Export(const fs::path & inFolder, const std::string & inApp, const std::vector<Ref> & inEntries,
                            RichTextFormat inRichText = RichTextFormat::kRTF)
{
  TranslatorContext context;
  context.folder = inFolder;
  context.appName = inApp;
  context.richText = inRichText;
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
  Check(ImportAppsFor("x.TXT") == std::vector<std::string>({ "Newton Works", "Notes" })
        && ImportAppsFor("x.vcf") == std::vector<std::string>({ "Names" }) && ImportAppsFor("x.doc").empty(),
        "notes: which app imports what");
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
  SetFrameSlot(note, SYMA(class), MakeSymbol("gadget"));
  SetFrameSlot(note, MakeSymbol("height"), MAKEINT(52));
  Check(ExportClassOf(package) == "*package*" && ExportClassOf(text) == "text" && ExportClassOf(note) == "gadget",
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
  Check(printed.find("gadget") != std::string::npos && printed.find("52") != std::string::npos,
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

/*-- graphics ----------------------------------------------------------------*/

/** An icon: 8 by 8, 1 bit, its left half black (bits: a FramBitmap: the
    base address, row bytes, a short, the bounds top left bottom right,
    big-endian; then the rows). */
Ref Icon()
{
  const unsigned char header[] = { 0,0,0,0, 0,2, 0,0, 0,0, 0,0, 0,8, 0,8 };
  RefVar bits(AllocateBinary(SYMA(bits), sizeof(header) + 16));
  std::memcpy(BinaryData(bits), header, sizeof(header));
  for (int i = 0; i < 8; ++i) {
    ((unsigned char *)BinaryData(bits))[sizeof(header) + 2 * i] = 0xF0;
    ((unsigned char *)BinaryData(bits))[sizeof(header) + 2 * i + 1] = 0x00;
  }
  RefVar icon(AllocateFrame());
  SetFrameSlot(icon, SYMA(bits), bits);
  return icon;
}

void TestGraphics()
{
  TempFolder folder("graphics");
  Pixels white;
  {
    Canvas canvas(20, 10);
    white = canvas.take();
  }
  int l, t, r, b;
  Check(white.width == 20 && white.height == 10 && white.gray(5, 5) == 255 && !white.inkBounds(l, t, r, b),
        "graphics: a white canvas");
  Pixels pattern = white;
  for (int x = 0; x < 10; ++x)
    pattern.rgb[(5 * 20 + x) * 3] = pattern.rgb[(5 * 20 + x) * 3 + 1] = pattern.rgb[(5 * 20 + x) * 3 + 2] = 0;
  Check(WritePNG(folder.path / "p.png", pattern), "graphics: a PNG written");
  Pixels back = ReadPNG(folder.path / "p.png");
  Check(back.width == 20 && back.height == 10 && back.gray(3, 5) == 0 && back.gray(15, 5) == 255 && back.gray(3, 4) == 255,
        "graphics: and read back");
  std::string png = PNGData(pattern);
  Check(png.size() > 8 && png.compare(1, 3, "PNG") == 0, "graphics: PNG data");
  Pixels part = Crop(pattern, 2, 4, 8, 7);
  Check(part.width == 6 && part.height == 3 && part.gray(0, 1) == 0 && part.gray(0, 0) == 255, "graphics: cropped");

  // a note: text, a polygon (NCX's ink: 0, the number of points, the points
  // as v, h; big-endian), a picture
  RefVar data(MakeArray(0));
  AddArraySlot(data, RefVar(MakeParagraph("text", 0, 0, 300, 20, 0)));
  RefVar poly(AllocateFrame());
  SetFrameSlot(poly, SYMA(viewStationery), MakeSymbol("poly"));
  SetFrameSlot(poly, SYMA(viewBounds), RefVar(MakeBounds(100, 40, 140, 90)));
  const unsigned char points[] = { 0,0, 0,3, 0,20, 0,10, 0,20, 0,30, 0,40, 0,30 };
  RefVar pointsBinary(AllocateBinary(MakeSymbol("polygonShape"), sizeof(points)));
  std::memcpy(BinaryData(pointsBinary), points, sizeof(points));
  SetFrameSlot(poly, MakeSymbol("points"), pointsBinary);
  SetFrameSlot(poly, SYMA(viewFormat), MAKEINT(2 << 8));           // vfPen(2)
  AddArraySlot(data, poly);
  RefVar pict(AllocateFrame());
  SetFrameSlot(pict, SYMA(viewStationery), MakeSymbol("pict"));
  SetFrameSlot(pict, SYMA(viewBounds), RefVar(MakeBounds(10, 50, 18, 58)));
  SetFrameSlot(pict, SYMA(icon), RefVar(Icon()));
  AddArraySlot(data, pict);
  RefVar pict2(Clone(pict));                         // an icon with its bounds
  RefVar icon2(Icon());
  SetFrameSlot(icon2, SYMA(bounds), RefVar(MakeBounds(0, 0, 8, 8)));
  SetFrameSlot(pict2, SYMA(icon), icon2);
  SetFrameSlot(pict2, SYMA(viewBounds), RefVar(MakeBounds(30, 50, 60, 80)));
  AddArraySlot(data, pict2);
  int left = -1, top = -1;
  Pixels note = RenderGraphics(data, &left, &top);
  Check(IsGraphicParagraph(poly) && IsGraphicParagraph(pict) && !IsGraphicParagraph(RefVar(GetArraySlot(data, 0))),
        "graphics: which paragraphs are graphics");
  Check(left == 8 && top == 38 && note.width == 142 - 8 && note.height == 92 - 38, "graphics: a note's graphics, cropped to them ("
        + std::to_string(left) + ", " + std::to_string(top) + ", " + std::to_string(note.width) + " x " + std::to_string(note.height) + ")");
  auto dark = [&](int x, int y) { return note.gray(x - left, y - top) < 64; };
  Check(dark(10, 50) && dark(13, 57) && !dark(15, 50) && !dark(17, 57), "graphics: the picture, at its place");
  Check(dark(30, 50) && dark(33, 57) && !dark(35, 50) && !dark(33, 59), "graphics: an icon, its size at the corner");
  Check(dark(110, 60) && dark(130, 70) && !dark(110, 80), "graphics: the polygon, at its place");
  Check(RenderGraphics(RefVar(MakeArray(0))).empty(), "graphics: none, no image");

  Pixels bitmap = RenderBitmap(RefVar(Icon()));
  Check(bitmap.width == 8 && bitmap.height == 8 && bitmap.gray(0, 0) < 64 && bitmap.gray(3, 7) < 64 && bitmap.gray(4, 0) > 192,
        "graphics: a bitmap at its size");

  RefVar image(AllocateFrame());
  SetFrameSlot(image, MakeSymbol("image"), RefVar(Icon()));
  SetFrameSlot(image, SYMA(title), RefVar(Str("Sketch")));
  Check(ExportClassOf(image) == "*image*", "graphics: an image entry");
  std::vector<fs::path> files = Export(folder.path, "Paint", { image });
  Pixels exported = files.size() == 1 ? ReadPNG(files[0]) : Pixels();
  Check(files.size() == 1 && files[0].filename() == "Sketch.png" && exported.width == 8 && exported.gray(1, 1) < 64
        && exported.gray(6, 1) > 192, "graphics: exported as a PNG");
}

/*-- ink ---------------------------------------------------------------------*/

std::string gInkNote;      // tests/fixtures/ink-note.nsof

/** A note drawn on Einstein (Matt, 2026-10-18): text, ink text (inkWord
    styles), polygons drawn with a 4-pixel pen, a box, a circle, a line,
    and freehand ink ('ink2). */
void TestInk()
{
  TempFolder folder("ink");
  std::string bytes = ReadFile(PathFromUTF8(gInkNote));
  RefVar note(UnflattenBytes(std::vector<uint8_t>(bytes.begin(), bytes.end())));
  RefVar data(GetFrameSlot(note, SYMA(data)));
  Check(IsArray(data) && Length(data) == 9, "ink: the note");
  int left = -1, top = -1;
  Pixels pixels = RenderGraphics(data, &left, &top);
  if (const char * show = std::getenv("NCX_SHOW"))
    WritePNG(PathFromUTF8(show), pixels);
  int l, t, r, b;
  bool drawn = pixels.inkBounds(l, t, r, b);
  // the freehand stroke on the right: a vertical line from the top, as
  // wide as the pen (vfPen(4)); the box's left edge, 4 wide too
  auto darkRun = [&](int y, int fromX, int toX) {
    int longest = 0, run = 0;
    for (int x = fromX; x < toX; ++x) {
      run = pixels.gray(x, y) < 128 ? run + 1 : 0;
      longest = std::max(longest, run);
    }
    return longest;
  };
  Check(darkRun(20, 170, 200) >= 3 && darkRun(20, 170, 200) <= 6, "ink: the freehand ink, the paragraph's pen wide ("
        + std::to_string(darkRun(20, 170, 200)) + ")");
  auto darkRunDown = [&](int x, int fromY, int toY) {
    int longest = 0, run = 0;
    for (int y = fromY; y < toY; ++y) {
      run = pixels.gray(x, y) < 128 ? run + 1 : 0;
      longest = std::max(longest, run);
    }
    return longest;
  };
  // the triangle's bottom edge (a polygon) crosses x = 50 near y = 183
  Check(darkRunDown(50, 170, 200) >= 3 && darkRunDown(50, 170, 200) <= 6, "ink: the polygons, the paragraph's pen wide ("
        + std::to_string(darkRunDown(50, 170, 200)) + ")");
  // the ink words: styles of class 'inkWord in the text of paragraph 3
  RefVar styles(GetFrameSlot(RefVar(GetArraySlot(data, 3)), SYMA(styles)));
  std::vector<Pixels> words;
  for (ArrayIndex i = 1; IsArray(styles) && i < Length(styles); i += 2) {
    Pixels word = RenderInkWord(RefVar(GetArraySlot(styles, i)));
    if (!word.empty())
      words.push_back(word);
  }
  if (const char * show = std::getenv("NCX_SHOW"))
    for (size_t i = 0; i < words.size(); ++i)
      WritePNG(PathFromUTF8(std::string(show) + "-word" + std::to_string(i) + ".png"), words[i]);
  int wl, wt, wr, wb;
  Check(words.size() == 2 && words[0].inkBounds(wl, wt, wr, wb) && words[1].inkBounds(wl, wt, wr, wb),
        "ink: two ink words in the text, drawn (" + std::to_string(words.size()) + ")");
  Check(drawn && left == 77 && top == 0, "ink: drawn, from the graphics' corner (" + std::to_string(left) + ", "
        + std::to_string(top) + "; " + std::to_string(pixels.width) + " x " + std::to_string(pixels.height) + ")");
}

/*-- richtext ----------------------------------------------------------------*/

void TestRichText()
{
  RichDocument document;
  RichParagraph para;
  TextStyle plain, bold, mono;
  bold.bold = true;
  bold.size = 18;
  mono.font = "Courier";
  para.addText("Caf\xC3\xA9 {braces} back\\slash ", plain);
  para.addText("bold", bold);
  para.addText("\ttab \xE2\x98\x91", mono);
  para.addPicture(std::string("\x89PNG\r\n", 6), 4, 3);
  para.style.alignment = Alignment::kCenter;
  para.style.leftIndent = 36;
  TabStop tab;
  tab.position = 72;
  tab.kind = TabStop::kRight;
  para.style.tabs.push_back(tab);
  document.paragraphs.push_back(para);
  RichParagraph second;
  TextStyle casual;
  casual.family = FontFamily::kHandwriting;
  casual.italic = casual.underline = true;
  second.addText("second", casual);
  document.paragraphs.push_back(second);
  document.margins[0] = 72;
  std::string rtf = WriteRTF(document);
  Check(rtf.compare(0, 6, "{\\rtf1") == 0 && rtf.find("Caf\\u233?") != std::string::npos && rtf.find("\\{braces\\}") != std::string::npos
        && rtf.find("back\\\\slash") != std::string::npos && rtf.find("\\u9745?") != std::string::npos
        && rtf.find("\\tab ") != std::string::npos, "richtext: RTF, escaped, Unicode as \\uN");
  Check(rtf.find("{\\pict\\pngblip\\picw4\\pich3\\picwgoal80\\pichgoal60\n89504e470d0a}") != std::string::npos
        && rtf.find("\\qc\\li720\\tqr\\tx1440") != std::string::npos && rtf.find("\\margl1440") != std::string::npos
        && rtf.find("\\fmodern\\fcharset0 Courier;") != std::string::npos && rtf.find("\\fscript\\fcharset0 Casual;") != std::string::npos,
        "richtext: a picture, the paragraph's alignment, indent and tabs, margins, fonts");
  RichDocument back = ReadRTF(rtf);
  Check(back.paragraphs.size() == 2 && back.text() == "Caf\xC3\xA9 {braces} back\\slash bold\ttab \xE2\x98\x91\nsecond",
        "richtext: read back, the text (pictures left out)");
  if (back.paragraphs.size() == 2) {
    const RichParagraph & p0 = back.paragraphs[0];
    Check(p0.style.alignment == Alignment::kCenter && p0.style.leftIndent == 36 && p0.style.tabs.size() == 1
          && p0.style.tabs[0].position == 72 && p0.style.tabs[0].kind == TabStop::kRight && back.margins[0] == 72,
          "richtext: the paragraph's style, the margins");
    Check(p0.runs.size() >= 3 && p0.runs[1].text == "bold" && p0.runs[1].style.bold && p0.runs[1].style.size == 18
          && p0.runs[2].style.font == "Courier" && back.paragraphs[1].runs[0].style.family == FontFamily::kHandwriting
          && back.paragraphs[1].runs[0].style.italic && back.paragraphs[1].runs[0].style.underline,
          "richtext: the runs' styles and fonts");
  }
  // RTF from elsewhere: a Word-like header, \'hh, \uN with a fallback, destinations skipped
  RichDocument word = ReadRTF("{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\froman Times New Roman;}{\\f1\\fswiss Arial;}}"
                              "{\\colortbl;\\red0\\green0\\blue0;}{\\*\\generator Word;}{\\info{\\title x}}"
                              "\\pard\\plain\\f1\\fs20 Gr\\'fc\\u223\\'dfe {\\i it}\\par\\pard\\f0 second\\line third}");
  Check(word.paragraphs.size() == 3 && word.paragraphs[0].text() == "Gr\xC3\xBC\xC3\x9F" "e it"
        && word.paragraphs[0].runs[0].style.size == 10 && word.paragraphs[0].runs.back().style.italic
        && word.paragraphs[1].runs[0].style.family == FontFamily::kFancy && word.paragraphs[2].text() == "third",
        "richtext: RTF from a word processor (" + word.text() + ")");
}

/*-- notesrtf ----------------------------------------------------------------*/

Ref ReadEntry(const std::string & inFile)
{
  std::string bytes = ReadFile(PathFromUTF8(inFile));
  return UnflattenBytes(std::vector<uint8_t>(bytes.begin(), bytes.end()));
}

std::string gFixtures;      // tests/fixtures

void TestNotesRTF()
{
  TempFolder folder("notesrtf");
  RefVar note(ReadEntry(gFixtures + "/ink-note.nsof"));
  RefVar checklist(AllocateFrame());
  SetFrameSlot(checklist, SYMA(class), MakeSymbol("checklist"));
  SetFrameSlot(checklist, SYMA(title), RefVar(Str("Shopping")));
  RefVar topics(MakeArray(0));
  const char * const kItems[] = { "Milk", "Bread", "Cheese" };
  for (int i = 0; i < 3; ++i) {
    RefVar topic(AllocateFrame());
    SetFrameSlot(topic, SYMA(text), RefVar(Str(kItems[i])));
    SetFrameSlot(topic, MakeSymbol("level"), MAKEINT(i == 2 ? 2 : 1));
    if (i == 1)
      SetFrameSlot(topic, MakeSymbol("mtgDone"), RA(TRUEREF));
    AddArraySlot(topics, topic);
  }
  SetFrameSlot(checklist, MakeSymbol("topics"), topics);
  std::vector<fs::path> files = Export(folder.path, "Notes", { note, checklist });
  Check(files.size() == 2 && files[1].filename() == "Shopping.rtf" && files[0].extension() == ".rtf",
        "notesrtf: a file each, titled, or dated");
  std::string rtf = files.size() == 2 ? ReadFile(files[0]) : std::string();
  Check(Count(rtf, "{\\pict\\pngblip") == 3 && rtf.find("\\fscript\\fcharset0 Casual;") != std::string::npos
        && rtf.find("{\\f1\\fs36 a I }") != std::string::npos,
        "notesrtf: the drawing and two ink words as pictures, the text in its font");
  RichDocument back = ReadRTF(rtf);
  Check(back.text() == "\na\n&\na I  \na", "notesrtf: the text in order, the drawing first (" + back.text() + ")");
  std::string list = files.size() == 2 ? ReadFile(files[1]) : std::string();
  Check(list.find("\\u9744? ") != std::string::npos && list.find("\\u9745? ") != std::string::npos
        && list.find("\\li36\\fi-360") == std::string::npos && list.find("\\li720") != std::string::npos
        && ReadRTF(list).text() == "\xE2\x98\x90 Milk\n\xE2\x98\x91 Bread\n\xE2\x98\x90 Cheese",
        "notesrtf: a checklist, boxes checked when done, indented by level");

  // RTFD: a folder, TXT.rtf and the pictures as its files (as Apple writes
  // them), plain RTF for a note without pictures (auto)
  files = Export(folder.path / "rtfd", "Notes", { note, checklist }, RichTextFormat::kRTFDWithPictures);
  std::string txt = files.size() == 2 ? ReadFile(files[0] / "TXT.rtf") : std::string();
  Check(files.size() == 2 && fs::is_directory(files[0]) && files[0].extension() == ".rtfd" && files[1].extension() == ".rtf"
        && fs::exists(files[0] / "image3.png") && ReadPNG(files[0] / "image1.png").width == 234
        && Count(txt, "{{\\NeXTGraphic image") == 3 && txt.find("\\appleaqc\n}\xAC}") != std::string::npos
        && txt.find("pngblip") == std::string::npos, "notesrtf: RTFD with the pictures as files, RTF without pictures");
  Check(ReadRTF(txt).text() == "\na\n&\na I  \na", "notesrtf: RTFD read back, the attachments left out");
  auto fromBundle = files.size() == 2 ? Import("Notes", files[0]) : decltype(Import("", fs::path()))();
  Check(fromBundle.size() == 1, "notesrtf: a note from an RTFD folder");

  // and back: a note from RTF keeps its styles
  WriteFile(folder.path / "Styled.rtf", "{\\rtf1\\ansi{\\fonttbl{\\f0\\fswiss Helvetica;}{\\f1\\fscript Casual;}}"
                                        "\\f0\\fs24 plain {\\b bold} {\\f1\\fs36 casual}\\par second}");
  auto entries = Import("Notes", folder.path / "Styled.rtf");
  RefVar para(entries.size() == 1 ? GetArraySlot(RefVar(GetFrameSlot(entries[0].second, SYMA(data))), 0) : (Ref)NILREF);
  RefVar styles(GetFrameSlot(para, SYMA(styles)));
  std::string text = Text(para, "text");
  bool styled = IsArray(styles) && Length(styles) >= 8 && RINT(GetArraySlot(styles, 0)) == 6
                && StyleOfFontSpec(RefVar(GetArraySlot(styles, 3))).bold
                && StyleOfFontSpec(RefVar(GetArraySlot(styles, 7))).family == FontFamily::kHandwriting
                && StyleOfFontSpec(RefVar(GetArraySlot(styles, 7))).size == 18;
  Check(entries.size() == 1 && text == "plain bold casual\nsecond" && styled && Text(entries[0].second, "title") == "Styled",
        "notesrtf: a note from RTF, its fonts, sizes and faces as styles");
}

/*-- works -------------------------------------------------------------------*/

void TestWorks()
{
  TempFolder folder("works");
  RefVar paper(ReadEntry(gFixtures + "/works-paper.nsof"));
  RefVar drawing(ReadEntry(gFixtures + "/works-drawing.nsof"));
  RefVar empty(ReadEntry(gFixtures + "/works-empty.nsof"));
  std::vector<fs::path> files = Export(folder.path, "Newton Works", { paper, drawing, empty });
  std::vector<std::string> names;
  for (const fs::path & f : files)
    names.push_back(UTF8FromPath(f.filename()));
  Check(names == std::vector<std::string>({ "Untitled Paper 1.rtf", "Untitled Drawing.png", "Untitled Paper.rtf" }),
        "works: a paper as RTF, a drawing as PNG, by their titles");
  std::string rtf = files.size() == 3 ? ReadFile(files[0]) : std::string();
  Check(rtf.find("Century Schoolbook;") != std::string::npos && rtf.find("\\fmodern\\fcharset0 Courier;") != std::string::npos
        && rtf.find("\\fs36\\ul Geneva }") != std::string::npos && rtf.find("\\fs28\\b  NewYork}") != std::string::npos
        && rtf.find("\\margl1440\\margt1440\\margr1440\\margb1440") != std::string::npos,
        "works: the paper's fonts (Works' own too), faces, sizes, margins");
  RichDocument back = ReadRTF(rtf);
  Check(back.text() == "\n\nThis is a very important paper\nfor the future of all of humanity.\nCasual Courier Geneva New York NewYork\n",
        "works: the paper's text, its paragraphs (txText by txData)");
  Pixels picture = files.size() == 3 ? ReadPNG(files[1]) : Pixels();
  if (const char * show = std::getenv("NCX_SHOW"))
    WritePNG(PathFromUTF8(std::string(show) + "-drawing.png"), picture);
  // the round rectangle's top edge; a football (a masked bitmap) dark inside
  auto dark = [&](int x, int y) { return picture.gray(x, y) < 128; };
  int darkCount = 0;
  for (int y = 60; y < 120; ++y)
    for (int x = 0; x < 80; ++x)
      darkCount += dark(x, y);
  Check(picture.width > 300 && picture.height > 140 && darkCount > 300, "works: the drawing, its shapes and bitmaps ("
        + std::to_string(picture.width) + " x " + std::to_string(picture.height) + ", " + std::to_string(darkCount) + ")");
  Check(files.size() == 3 && ReadRTF(ReadFile(files[2])).text().empty(), "works: an empty paper");

  // a QuickFigure sheet: cell 0 "Item", 1 "Price", 40 "Tea, green", 41 =A2*2, 81 2.5
  std::vector<uint8_t> cells(8, 0), addresses(4 * 82, 0);
  auto addCell = [&](int inIndex, RefArg inCell) {
    std::vector<uint8_t> nsof = FlattenToBytes(inCell);
    uint32_t offset = (uint32_t)cells.size(), length = (uint32_t)nsof.size();
    for (int k = 0; k < 4; ++k)
      cells.push_back((uint8_t)(length >> (24 - 8 * k)));
    cells.insert(cells.end(), 4, 0);
    cells.insert(cells.end(), nsof.begin(), nsof.end());
    for (int k = 0; k < 4; ++k)
      addresses[inIndex * 4 + k] = (uint8_t)(offset >> (24 - 8 * k));
  };
  auto cell = [&](const char * inSlot, RefArg inValue) {
    RefVar c(AllocateFrame());
    SetFrameSlot(c, MakeSymbol(inSlot), inValue);
    return Ref(c);
  };
  addCell(0, RefVar(cell("value", RefVar(Str("Item")))));
  addCell(1, RefVar(cell("value", RefVar(Str("Price")))));
  addCell(40, RefVar(cell("value", RefVar(Str("Tea, green")))));
  addCell(41, RefVar(cell("viewFormula", RefVar(Str("A2*2")))));
  addCell(81, RefVar(cell("value", RefVar(MakeReal(2.5)))));
  RefVar sheet(AllocateFrame());
  SetFrameSlot(sheet, SYMA(class), MakeSymbol("QFNewt:donv"));
  SetFrameSlot(sheet, SYMA(title), RefVar(Str("Prices")));
  RefVar cellsBinary(AllocateBinary(SYMA(binary), cells.size()));
  std::memcpy(BinaryData(cellsBinary), cells.data(), cells.size());
  RefVar addressesBinary(AllocateBinary(SYMA(binary), addresses.size()));
  std::memcpy(BinaryData(addressesBinary), addresses.data(), addresses.size());
  SetFrameSlot(sheet, MakeSymbol("cells"), cellsBinary);
  SetFrameSlot(sheet, MakeSymbol("cellAddresses"), addressesBinary);
  SetFrameSlot(sheet, MakeSymbol("lastCellIndex"), MAKEINT(81 * 4));
  files = Export(folder.path, "Newton Works", { sheet });
  Check(files.size() == 1 && files[0].filename() == "Prices.csv"
        && ReadFile(files[0]) == "Item,Price\n\"Tea, green\",=A2*2\n,2.5\n", "works: a QuickFigure sheet as CSV ("
        + (files.size() == 1 ? ReadFile(files[0]) : std::string()) + ")");

  // and from a file: a paper
  WriteFile(folder.path / "Memo.txt", "First line\nSecond line\n");
  auto entries = Import("Newton Works", folder.path / "Memo.txt");
  RefVar memo(entries.size() == 1 ? (Ref)entries[0].second : (Ref)NILREF);
  RefVar saveData(GetFrameSlot(memo, MakeSymbol("saveData")));
  Check(entries.size() == 1 && entries[0].first == "NewtWorks" && EQRef(GetFrameSlot(memo, SYMA(class)), MakeSymbol("paper"))
        && Text(saveData, "text") == "First line\nSecond line\n" && Text(memo, "title") == "Memo"
        && IsArray(GetFrameSlot(saveData, MakeSymbol("rulers"))), "works: a paper from text");
  // and the paper back out
  files = Export(folder.path / "again", "Newton Works", { memo });
  Check(files.size() == 1 && ReadRTF(ReadFile(files[0])).text() == "First line\nSecond line", "works: and out again (its last line end ends the paragraph)");
}

/*-- audio -------------------------------------------------------------------*/

Ref Recording(const std::string & inSamples, RefArg inRate, int inDataType, int inCompression, const char * inCodec)
{
  RefVar sound(AllocateFrame());
  RefVar samples(AllocateBinary(SYMA(samples), inSamples.size()));
  std::memcpy(BinaryData(samples), inSamples.data(), inSamples.size());
  SetFrameSlot(sound, SYMA(samples), samples);
  SetFrameSlot(sound, SYMA(samplingRate), inRate);
  SetFrameSlot(sound, SYMA(dataType), MAKEINT(inDataType));
  SetFrameSlot(sound, SYMA(compressionType), MAKEINT(inCompression));
  if (inCodec)
    SetFrameSlot(sound, SYMA(codecName), RefVar(Str(inCodec)));
  RefVar sounds(MakeArray(1));
  SetArraySlot(sounds, 0, sound);
  RefVar soundData(AllocateFrame());
  SetFrameSlot(soundData, MakeSymbol("sounds"), sounds);
  RefVar paper(AllocateFrame());
  SetFrameSlot(paper, MakeSymbol("soundData"), soundData);
  RefVar entry(AllocateFrame());
  SetFrameSlot(entry, SYMA(class), MakeSymbol("soundPaper:NSG"));
  SetFrameSlot(entry, MakeSymbol("soundPaper:NSG"), paper);
  SetFrameSlot(entry, SYMA(title), RefVar(Str("Memo")));
  return entry;
}

uint32_t LE(const std::string & inData, size_t inAt, int inBytes)
{
  uint32_t value = 0;
  for (int i = inBytes - 1; i >= 0; --i)
    value = value << 8 | (unsigned char)inData[inAt + i];
  return value;
}

void TestAudio()
{
  TempFolder folder("audio");
  // 8 bits at 11025 Hz (a Fixed), 16 bits big-endian, mu-law, IMA
  RefVar fixedRate(AllocateBinary(SYMA(binary), 4));
  const unsigned char rate11025[] = { 0x2B, 0x11, 0x00, 0x00 };
  std::memcpy(BinaryData(fixedRate), rate11025, 4);
  RefVar eight(Recording(std::string("\x80\xFF\x00\x80", 4), fixedRate, 8, 0, nullptr));
  RefVar sixteen(Recording(std::string("\x12\x34\xFF\xFE", 4), RefVar(MakeReal(22050.0)), 16, 6, nullptr));
  RefVar muLaw(Recording(std::string("\xFF\x00", 2), RefVar(MAKEINT(8000)), 8, 1, "TMuLawCodec"));
  RefVar ima(Recording(std::string(34, '\x11'), RefVar(MAKEINT(22026)), 16, 0, "TIMACodec"));
  std::vector<fs::path> files = Export(folder.path, "Notes", { eight, sixteen, muLaw, ima });
  std::vector<std::string> names;
  for (const fs::path & f : files)
    names.push_back(UTF8FromPath(f.filename()));
  Check(names == std::vector<std::string>({ "Memo.wav", "Memo-2.wav", "Memo-3.wav", "Memo.aifc" }),
        "audio: recordings as WAV, another codec's as AIFF-C");
  if (files.size() != 4)
    return;
  std::string w8 = ReadFile(files[0]), w16 = ReadFile(files[1]), wmu = ReadFile(files[2]), aifc = ReadFile(files[3]);
  Check(w8.compare(0, 4, "RIFF") == 0 && w8.compare(8, 8, "WAVEfmt ") == 0 && LE(w8, 24, 4) == 11025 && LE(w8, 34, 2) == 8
        && w8.substr(44) == std::string("\x80\xFF\x00\x80", 4), "audio: 8 bits as they are, the Fixed rate");
  Check(LE(w16, 24, 4) == 22050 && LE(w16, 34, 2) == 16 && w16.substr(44) == std::string("\x34\x12\xFE\xFF", 4),
        "audio: 16 bits little-endian, a real rate");
  Check(LE(wmu, 24, 4) == 8000 && LE(wmu, 34, 2) == 16 && LE(wmu, 40, 4) == 4 && (int16_t)LE(wmu, 44, 2) == 0
        && (int16_t)LE(wmu, 46, 2) == -32124, "audio: mu-law decoded");
  Check(aifc.compare(0, 4, "FORM") == 0 && aifc.compare(8, 4, "AIFC") == 0 && aifc.find("ima4\x09TIMACodec") != std::string::npos
        && aifc.find("SSND") != std::string::npos, "audio: IMA as AIFF-C, the codec named");

  // an MP3 for MAD Max: its bytes, its ID3v2.3 tags (title in UTF-16)
  std::string id3 = "ID3\x03";
  id3 += std::string("\0\0", 2);
  std::string frames;
  auto frame = [&](const char * inId, const std::string & inData) {
    frames += inId;
    uint32_t size = (uint32_t)inData.size();
    for (int k = 3; k >= 0; --k)
      frames += (char)(size >> (8 * k));
    frames += std::string("\0\0", 2);
    frames += inData;
  };
  frame("TIT2", std::string("\x01\xFF\xFE" "A\0d\0a\0", 9));
  frame("TPE1", std::string("\x00" "Lovelace", 9));
  frame("TALB", std::string("\x03" "Engines", 8));
  uint32_t size = (uint32_t)frames.size();
  for (int k = 3; k >= 0; --k)
    id3 += (char)((size >> (7 * k)) & 0x7F);
  std::string mp3 = id3 + frames + std::string("\xFF\xFB\x90\x00 audio", 10);
  WriteFile(folder.path / "song.mp3", mp3);
  Check(ImportAppsFor("x.mp3") == std::vector<std::string>({ "MAD Max:40Hz" }), "audio: MAD Max imports MP3s");
  auto entries = Import("MAD Max:40Hz", folder.path / "song.mp3");
  RefVar song(entries.size() == 1 ? (Ref)entries[0].second : (Ref)NILREF);
  RefVar samples(GetFrameSlot(RefVar(GetFrameSlot(song, MakeSymbol("MP3Data"))), SYMA(samples)));
  Check(entries.size() == 1 && entries[0].first == "Audio" && EQRef(GetFrameSlot(song, SYMA(class)), MakeSymbol("MP3Data"))
        && IsBinary(samples) && Length(samples) == mp3.size() && Text(song, "title") == "Ada" && Text(song, "artist") == "Lovelace"
        && Text(song, "album") == "Engines", "audio: an MP3 as MAD Max's entry, its ID3 tags");
  files = Export(folder.path / "out", "MAD Max:40Hz", { song });
  Check(files.size() == 1 && files[0].filename() == "Ada.mp3" && ReadFile(files[0]) == mp3, "audio: and back as the MP3");
}

/*-- golden ------------------------------------------------------------------*/

/** The cards of a vCard text. */
std::vector<std::vector<ContentLine>> Cards(const std::string & inText)
{
  std::vector<std::vector<ContentLine>> cards;
  for (ContentLine & line : ParseContentLines(inText)) {
    if (line.name == "BEGIN")
      cards.emplace_back();
    else if (line.name != "END" && !cards.empty())
      cards.back().push_back(line);
  }
  return cards;
}

/** A card as what it holds: its N, its phones (number and types, without
    "pref"), its addresses. */
std::string CardSummary(const std::vector<ContentLine> & inCard)
{
  std::vector<std::string> phones, addresses;
  std::string name;
  for (const ContentLine & line : inCard) {
    if (line.name == "N")
      name = line.value;
    else if (line.name == "ADR")
      addresses.push_back(line.value);
    else if (line.name == "TEL") {
      std::vector<std::string> types;
      for (const auto & p : line.params)
        if (p.first == "TYPE")
          for (const std::string & t : SplitValue(p.second, ','))
            if (Upper(t) != "PREF")
              types.push_back(Upper(t));
      std::sort(types.begin(), types.end());
      std::string text = UnescapeText(line.value) + " (";
      for (const std::string & t : types)
        text += t + " ";
      phones.push_back(text + ")");
    }
  }
  std::sort(phones.begin(), phones.end());
  std::sort(addresses.begin(), addresses.end());
  std::string summary = "N:" + name;
  for (const std::string & p : phones)
    summary += " TEL:" + p;
  for (const std::string & a : addresses)
    summary += " ADR:" + a;
  return summary;
}

/** Events as what they hold: start, duration, title (unescaped, without a
    line end at its end). */
std::set<std::string> Events(const std::string & inText)
{
  std::set<std::string> events;
  std::string start, duration, summary;
  for (const ContentLine & line : ParseContentLines(inText)) {
    if (line.name == "DTSTART")
      start = line.value;
    else if (line.name == "DURATION")
      duration = line.value;
    else if (line.name == "SUMMARY") {
      summary = UnescapeText(ReplaceAll(line.value, "\\r", ""));          // (the Cocoa app's \r)
      while (!summary.empty() && (summary.back() == '\n' || summary.back() == '\r'))
        summary.pop_back();
    } else if (line.name == "END" && line.value == "VEVENT")
      events.insert(start + " " + duration + " " + summary);
  }
  return events;
}

/** How much of a picture's ink another has too, aligned by the right and
    bottom of their ink (within 2 pixels of a dark one): 0 ... 1. */
double InkCovered(const Pixels & inThis, const Pixels & inOther)
{
  int l1, t1, r1, b1, l2, t2, r2, b2;
  if (!inThis.inkBounds(l1, t1, r1, b1) || !inOther.inkBounds(l2, t2, r2, b2))
    return 0;
  int dx = r2 - r1, dy = b2 - b1;
  size_t ink = 0, covered = 0;
  for (int y = t1; y < b1; ++y)
    for (int x = l1; x < r1; ++x) {
      if (inThis.gray(x, y) >= 128)
        continue;
      ++ink;
      bool found = false;
      for (int ny = -2; ny <= 2 && !found; ++ny)
        for (int nx = -2; nx <= 2 && !found; ++nx)
          found = inOther.gray(x + dx + nx, y + dy + ny) < 160;
      covered += found;
    }
  return ink ? (double)covered / ink : 0;
}

/** Text lines without the Cocoa app's "-ink-" and blanks around, sorted. */
std::vector<std::string> SortedLines(std::string inText)
{
  inText = ReplaceAll(inText, "-ink-", "") + "\n";
  std::vector<std::string> lines;
  for (size_t start = 0, end; (end = inText.find('\n', start)) != std::string::npos; start = end + 1) {
    std::string line = inText.substr(start, end - start);
    while (!line.empty() && line.back() == ' ')
      line.pop_back();
    if (!line.empty())
      lines.push_back(line);
  }
  std::sort(lines.begin(), lines.end());
  return lines;
}

/** NCX against what the Cocoa NCX exported of the same backup
    (tests/fixtures/cocoa/README.md: what it got wrong is allowed). */
void TestGolden()
{
  TempFolder folder("golden");
  fs::path cocoa = PathFromUTF8(gFixtures) / "cocoa";
  fs::path backup = folder.path / "e.newtondevice";
  fs::copy_file(cocoa / "einstein.newtondevice", backup);
  std::string error;
  std::unique_ptr<DeviceFile> file = DeviceFile::Open(backup, DeviceFile::kReadOnly, error);
  Check(file != nullptr, "golden: the backup " + error);
  if (!file)
    return;
  auto exportApp = [&](const char * inApp, RichTextFormat inRich = RichTextFormat::kRTF) {
    std::optional<AppRecord> app = file->findApp(inApp);
    fs::path out = folder.path / inApp;
    return app ? ExportApp(*file, *app, 0, out, {}, inRich).files : std::vector<fs::path>();
  };
  auto named = [](const std::vector<fs::path> & inFiles, const std::string & inName) {
    for (const fs::path & f : inFiles)
      if (UTF8FromPath(f.filename()) == inName)
        return f;
    return fs::path();
  };

  // Names: every card the Cocoa app wrote, with the same name, phones and
  // addresses (it wrote no ORG or FN for companies: NCX does)
  std::vector<fs::path> names = exportApp("Names");
  auto ours = Cards(ReadFile(named(names, "Newton Names.vcf")));
  std::vector<std::vector<ContentLine>> theirs;
  for (const char * f : { "names-person.vcf", "names-company.vcf", "names-owner.vcf" })
    for (auto & card : Cards(ReadFile(cocoa / f)))
      theirs.push_back(card);
  std::multiset<std::string> ourCards, theirCards;
  for (auto & card : ours)
    ourCards.insert(CardSummary(card));
  for (auto & card : theirs)
    theirCards.insert(CardSummary(card));
  Check(theirs.size() == 7 && ourCards == theirCards, "golden: Names, the same 7 cards (names, phones, addresses)");
  size_t companies = 0;
  for (auto & card : ours)
    for (const ContentLine & line : card)
      companies += line.name == "ORG";
  Check(companies == 4, "golden: Names, NCX keeps the companies the Cocoa app lost");

  // Dates: the same events (the Cocoa app wrote one twice)
  std::vector<fs::path> dates = exportApp("Dates");
  std::set<std::string> ourEvents = Events(ReadFile(named(dates, "Newton Dates.ics")));
  std::set<std::string> theirEvents = Events(ReadFile(cocoa / "dates.ics"));
  Check(theirEvents.size() == 7 && ourEvents == theirEvents, "golden: Dates, the same 7 events (start, duration, title)");

  // Calls: printed as NewtonScript the same way (the Cocoa app's had one of the two)
  std::vector<fs::path> calls = exportApp("Calls");
  std::string ourCalls = ReadFile(named(calls, "Newton Calls.text")), theirCalls = ReadFile(cocoa / "calls.text");
  Check(!theirCalls.empty() && ourCalls.compare(0, theirCalls.size() - 1, theirCalls, 0, theirCalls.size() - 1) == 0,
        "golden: Calls, NewtonScript printed byte for byte as the Cocoa app did");

  // the package, byte for byte
  std::vector<fs::path> packages = exportApp("Packages");
  Check(ReadFile(named(packages, "hello-SIG.newtonpkg")) == ReadFile(cocoa / "hello-SIG.newtonpkg"),
        "golden: the package, byte for byte");

  // Works: the paper's text and faces (the Cocoa app lost Courier, Schoolbook
  // and the underline); the drawing's shapes (it didn't draw the bitmaps)
  std::vector<fs::path> works = exportApp("Newton Works");
  RichDocument ourPaper = ReadRTF(ReadFile(named(works, "Untitled Paper 1.rtf")));
  RichDocument theirPaper = ReadRTF(ReadFile(cocoa / "works-paper.rtf"));
  std::string ourText = ourPaper.text(), theirText = theirPaper.text();
  while (!theirText.empty() && theirText.back() == '\n')
    theirText.pop_back();
  while (!ourText.empty() && ourText.back() == '\n')
    ourText.pop_back();
  auto boldNewYork = [](const RichDocument & inDocument) {
    for (const RichParagraph & p : inDocument.paragraphs)
      for (const TextRun & r : p.runs)
        if (r.text.find("NewYork") != std::string::npos)
          return r.style.bold && r.style.size == 14;
    return false;
  };
  Check(ourText == theirText && boldNewYork(ourPaper) && boldNewYork(theirPaper), "golden: the Works paper, its text and faces");
  Pixels ourDrawing = ReadPNG(named(works, "Untitled Drawing.png")), theirDrawing = ReadPNG(cocoa / "works-drawing.png");
  double drawn = InkCovered(theirDrawing, ourDrawing);
  Check(drawn > 0.95, "golden: the Works drawing, the Cocoa app's shapes all there (" + std::to_string((int)(drawn * 100)) + "%)");

  // the ink note: the same text (the Cocoa app: "-ink-" for ink words, the
  // paragraphs in the data's order); the same graphics
  std::vector<fs::path> notes = exportApp("Notes", RichTextFormat::kRTFDWithPictures);
  fs::path inkNote;
  for (const fs::path & f : notes)
    if (fs::is_directory(f))
      inkNote = f;
  RichDocument ourNote = ReadRTF(ReadFile(inkNote / "TXT.rtf"));
  RichDocument theirNote = ReadRTF(ReadFile(cocoa / "ink-note.rtf"));
  Check(!inkNote.empty() && SortedLines(ourNote.text()) == SortedLines(theirNote.text()),
        "golden: the ink note, the same lines of text (" + ourNote.text() + " / " + theirNote.text() + ")");
  double graphics = InkCovered(ReadPNG(cocoa / "ink-note-graphics.png"), ReadPNG(inkNote / "image1.png"));
  Check(graphics > 0.9, "golden: the ink note's graphics as the Cocoa app drew them (" + std::to_string((int)(graphics * 100)) + "%)");
}

} // namespace


int main(int argc, char * argv[])
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::string section = argc > 1 ? argv[1] : "all";
  gFixture = argc > 2 ? argv[2] : "";
  gInkNote = argc > 2 ? argv[2] : "";
  gFixtures = argc > 2 ? argv[2] : "";
  InitCore();
  struct { const char * name; void (*test)(); } sections[] = {
    { "lines", TestLines }, { "names", TestNames }, { "dates", TestDates }, { "notes", TestNotes },
    { "builtin", TestBuiltin }, { "graphics", TestGraphics }, { "richtext", TestRichText }, { "audio", TestAudio },
    { "ink", TestInk }, { "notesrtf", TestNotesRTF }, { "works", TestWorks },
    { "golden", TestGolden }, { "transfer", TestTransfer } };
  bool found = false;
  for (auto & s : sections) {
    std::string name = s.name;
    bool needsFixture = name == "transfer" || name == "ink" || name == "notesrtf" || name == "works" || name == "golden";
    if (section == s.name || (section == "all" && !needsFixture)) {
      try {
        CallNewton([&] { s.test(); });
      } catch (const std::exception & e) {
        Check(false, std::string(s.name) + ": " + e.what());
      }
      found = true;
    }
  }
  if (!found) {
    std::fprintf(stderr, "no section \"%s\"\n", section.c_str());
    return 2;
  }
  std::printf("%s\n", gFailed ? "FAILED" : "all ok");
  return gFailed ? 1 : 0;
}
