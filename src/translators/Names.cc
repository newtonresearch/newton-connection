/*
 File: Names.cc

 Names cards and vCards (the Cocoa app's NameToVCard, NameFromVCard; it
 left the vCard text to Apple's Contacts framework, here it is vCard 3.0
 as Apple writes it: relations and the anniversary as X-ABRELATEDNAMES and
 X-ABDATE with an X-ABLabel, "_$!<Friend>!$_").

 Export: every card of class 'person, 'owner or 'company of an app into
 one "Newton <app>.vcf". Import: a .vcf with any number of cards into the
 Names soup.

 A card:  name {first, last, honorific, title}, company, title (the job),
          address, address2, city, region, postal_code, country,
          addresses [{the same}], phones [strings of class 'homePhone,
          'workPhone, 'faxPhone, 'homefaxPhone, 'carPhone, 'mobilePhone,
          'phone], pagers [{pagerNum}], email, emailAddrs [{email}], URLs,
          bday, anniversary (Newton dates), names [{class: 'person, title
          (the relation), first, last}], notes [paragraphs]
 */

#include "translators/Translator.h"
#include "translators/ContentLines.h"
#include "base/Dates.h"
#include "base/NewtonStrings.h"

#include "RSSymbols.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>

namespace ncx {

namespace {

/** A Newton date (minutes since 1904, a day) as "1990-01-31". */
std::string VCardDate(int64_t inMinutes)
{
  CivilTime t = UTCTime(TimeFromNewton(inMinutes));
  char text[16];
  std::snprintf(text, sizeof(text), "%04d-%02d-%02d", t.year, t.month, t.day);
  return text;
}

/** "1990-01-31", "19900131" or with a time after it: a Newton date (the
    day), or -1. */
int64_t NewtonFromVCardDate(const std::string & inText)
{
  int y = 0, m = 0, d = 0;
  if (std::sscanf(inText.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3 && std::sscanf(inText.c_str(), "%4d%2d%2d", &y, &m, &d) != 3)
    return -1;
  CivilTime t;
  t.year = y;
  t.month = m;
  t.day = d;
  return NewtonFromTime(TimeFromUTC(t));
}

/** Affiliations the Address Book knows by a label of its own. */
const char * const kAffiliates[][2] = {
  { "friend", "_$!<Friend>!$_" }, { "mother", "_$!<Mother>!$_" }, { "brother", "_$!<Brother>!$_" },
  { "assistant", "_$!<Assistant>!$_" }, { "father", "_$!<Father>!$_" }, { "parent", "_$!<Parent>!$_" },
  { "partner", "_$!<Partner>!$_" }, { "sister", "_$!<Sister>!$_" }, { "child", "_$!<Child>!$_" },
  { "spouse", "_$!<Spouse>!$_" }, { "manager", "_$!<Manager>!$_" } };

std::string Lower(std::string inText)
{
  for (char & c : inText)
    c = (char)std::tolower((unsigned char)c);
  return inText;
}

std::string AffiliateLabel(const std::string & inTitle)
{
  if (inTitle.empty())
    return "also";
  for (const auto & a : kAffiliates)
    if (Lower(inTitle) == a[0])
      return a[1];
  return inTitle;
}

/** "_$!<Friend>!$_" -> "Friend" */
std::string LabelText(const std::string & inLabel)
{
  if (inLabel.size() > 8 && inLabel.compare(0, 4, "_$!<") == 0 && inLabel.compare(inLabel.size() - 4, 4, ">!$_") == 0)
    return inLabel.substr(4, inLabel.size() - 8);
  return inLabel;
}

/** An address of a card (or one of its addresses) as an ADR line; empty
    value if it has none. */
ContentLine AddressLine(RefArg inFrame)
{
  std::string street = StringSlot(inFrame, "address"), street2 = StringSlot(inFrame, "address2");
  if (!street2.empty())
    street = street.empty() ? street2 : street + "\n" + street2;
  std::string city = StringSlot(inFrame, "city"), region = StringSlot(inFrame, "region"),
              postal = StringSlot(inFrame, "postal_code"), country = StringSlot(inFrame, "country");
  ContentLine line("ADR", "");
  if (street.empty() && city.empty() && region.empty() && postal.empty() && country.empty())
    return line;
  line.addParam("TYPE", "HOME");
  line.value = ";;" + EscapeText(street) + ";" + EscapeText(city) + ";" + EscapeText(region) + ";" + EscapeText(postal)
             + ";" + EscapeText(country);
  return line;
}

const struct { const char * phoneClass; const char * types; } kPhoneTypes[] = {
  { "homePhone", "HOME,VOICE" }, { "workPhone", "WORK,VOICE" }, { "faxPhone", "WORK,FAX" },
  { "homefaxPhone", "HOME,FAX" }, { "carPhone", "CELL,CAR" }, { "mobilePhone", "CELL,VOICE" } };


class NameToVCard : public Exporter
{
public:
  std::filesystem::path exportEntry(RefArg inEntry) override
  {
    // nothing useful in it (the Cocoa app's test)
    if (!IsFrame(inEntry) || Length(inEntry) < 5)
      return std::filesystem::path();
    std::vector<ContentLine> lines;
    lines.emplace_back("BEGIN", "VCARD");
    lines.emplace_back("VERSION", "3.0");
    lines.emplace_back("PRODID", "-//Newton Research//NCX//EN");

    RefVar name(GetFrameSlot(inEntry, SYMA(name)));
    std::string first = StringSlot(name, "first"), last = StringSlot(name, "last"),
                honorific = StringSlot(name, "honorific"), suffix = StringSlot(name, "title");
    std::string company = StringSlot(inEntry, "company");
    if (company.empty())
      company = StringSlot(name, "company");
    std::string full = FullName(name);
    bool isCompany = full.empty() || EQRef(GetFrameSlot(inEntry, SYMA(class)), SYMA(company));
    lines.emplace_back("N", EscapeText(last) + ";" + EscapeText(first) + ";;" + EscapeText(honorific) + ";" + EscapeText(suffix));
    lines.emplace_back("FN", EscapeText(full.empty() ? company : full));
    if (!company.empty())
      lines.emplace_back("ORG", EscapeText(company) + ";");
    std::string job = StringSlot(inEntry, "title");
    if (!job.empty())
      lines.emplace_back("TITLE", EscapeText(job));
    if (isCompany && !company.empty())
      lines.emplace_back("X-ABShowAs", "COMPANY");

    // addresses
    ContentLine address = AddressLine(inEntry);
    if (!address.value.empty())
      lines.push_back(address);
    RefVar list(GetFrameSlot(inEntry, MakeSymbol("addresses")));
    for (ArrayIndex i = 0, n = IsArray(list) ? Length(list) : 0; i < n; ++i) {
      ContentLine more = AddressLine(RefVar(GetArraySlot(list, i)));
      if (!more.value.empty())
        lines.push_back(more);
    }

    // phones and pagers
    list = GetFrameSlot(inEntry, MakeSymbol("phones"));
    for (ArrayIndex i = 0, n = IsArray(list) ? Length(list) : 0; i < n; ++i) {
      RefVar phone(GetArraySlot(list, i));
      if (!IsString(phone) || UTF8FromString(phone).empty())
        continue;
      std::string types = "MAIN";
      RefVar phoneClass(ClassOf(phone));
      for (const auto & t : kPhoneTypes)
        if (EQRef(phoneClass, MakeSymbol(t.phoneClass)))
          types = t.types;
      lines.push_back(ContentLine("TEL", EscapeText(UTF8FromString(phone))).addParam("TYPE", types));
    }
    list = GetFrameSlot(inEntry, MakeSymbol("pagers"));
    for (ArrayIndex i = 0, n = IsArray(list) ? Length(list) : 0; i < n; ++i) {
      std::string pager = StringSlot(RefVar(GetArraySlot(list, i)), "pagerNum");
      if (!pager.empty())
        lines.push_back(ContentLine("TEL", EscapeText(pager)).addParam("TYPE", "PAGER"));
    }

    // e-mail, URLs
    std::string email = StringSlot(inEntry, "email");
    if (!email.empty())
      lines.push_back(ContentLine("EMAIL", EscapeText(email)).addParam("TYPE", "INTERNET,WORK"));
    list = GetFrameSlot(inEntry, MakeSymbol("emailAddrs"));
    for (ArrayIndex i = 0, n = IsArray(list) ? Length(list) : 0; i < n; ++i) {
      std::string more = StringSlot(RefVar(GetArraySlot(list, i)), "email");
      if (!more.empty())
        lines.push_back(ContentLine("EMAIL", EscapeText(more)).addParam("TYPE", "INTERNET,WORK"));
    }
    list = GetFrameSlot(inEntry, MakeSymbol("URLs"));
    for (ArrayIndex i = 0, n = IsArray(list) ? Length(list) : 0; i < n; ++i) {
      RefVar url(GetArraySlot(list, i));
      if (IsString(url) && !UTF8FromString(url).empty())
        lines.push_back(ContentLine("URL", EscapeText(UTF8FromString(url))).addParam("TYPE", "WORK"));
    }

    // dates
    RefVar date(GetFrameSlot(inEntry, MakeSymbol("bday")));
    if (ISINT(date))
      lines.emplace_back("BDAY", VCardDate(RINT(date)));
    int item = 0;
    date = GetFrameSlot(inEntry, MakeSymbol("anniversary"));
    if (ISINT(date)) {
      std::string group = "item" + std::to_string(++item);
      ContentLine line("X-ABDATE", VCardDate(RINT(date)));
      line.group = group;
      lines.push_back(line);
      ContentLine label("X-ABLabel", "_$!<Anniversary>!$_");
      label.group = group;
      lines.push_back(label);
    }

    // relations
    list = GetFrameSlot(inEntry, MakeSymbol("names"));
    for (ArrayIndex i = 0, n = IsArray(list) ? Length(list) : 0; i < n; ++i) {
      RefVar other(GetArraySlot(list, i));
      std::string otherName = FullName(other);
      if (!IsFrame(other) || otherName.empty())
        continue;
      std::string group = "item" + std::to_string(++item);
      ContentLine line("X-ABRELATEDNAMES", EscapeText(otherName));
      line.group = group;
      lines.push_back(line);
      ContentLine label("X-ABLabel", EscapeText(AffiliateLabel(StringSlot(other, "title"))));
      label.group = group;
      lines.push_back(label);
    }

    // notes, without the CATEGORIES: lines the Cocoa app's import left there
    std::string notes;
    std::istringstream in(ParagraphsText(RefVar(GetFrameSlot(inEntry, MakeSymbol("notes")))));
    for (std::string line; std::getline(in, line);)
      if (line.find("CATEGORIES:") == std::string::npos)
        notes += (notes.empty() ? "" : "\n") + line;
    if (!notes.empty())
      lines.emplace_back("NOTE", EscapeText(notes));

    lines.emplace_back("END", "VCARD");
    for (const ContentLine & line : lines)
      mText += FormatContentLine(line);
    return std::filesystem::path();
  }

  std::filesystem::path end() override
  {
    if (mText.empty())
      return std::filesystem::path();
    return write(mText, "Newton " + mContext.appName, "vcf");
  }

private:
  std::string mText;
};


/*-- import ------------------------------------------------------------------*/

/** An ADR value into the slots of a card (or one of its addresses). */
void SetAddress(RefArg ioFrame, const std::string & inValue)
{
  std::vector<std::string> parts = SplitValue(inValue, ';');
  parts.resize(7);
  std::string street = parts[2];
  if (!parts[1].empty())
    street = street.empty() ? parts[1] : street + "\n" + parts[1];        // the extended address
  size_t newline = street.find('\n');
  auto set = [&](const char * inSlot, const std::string & inText) {
    if (!inText.empty())
      SetFrameSlot(ioFrame, MakeSymbol(inSlot), RefVar(MakeStringFromUTF8(inText)));
  };
  set("address", street.substr(0, newline));
  if (newline != std::string::npos)
    set("address2", street.substr(newline + 1));
  set("city", parts[3]);
  set("region", parts[4]);
  set("postal_code", parts[5]);
  set("country", parts[6]);
}


class NameFromVCard : public Importer
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
    // the cards: BEGIN:VCARD ... END:VCARD
    std::vector<ContentLine> card;
    bool inCard = false;
    for (ContentLine & line : ParseContentLines(text)) {
      if (line.name == "BEGIN" && Upper(line.value) == "VCARD") {
        inCard = true;
        card.clear();
      } else if (line.name == "END" && Upper(line.value) == "VCARD") {
        if (inCard)
          mCards.push_back(card);
        inCard = false;
      } else if (inCard) {
        card.push_back(std::move(line));
      }
    }
    return true;
  }

  Ref next(std::string &) override
  {
    if (mNext >= mCards.size())
      return NILREF;
    return makeEntry(mCards[mNext++]);
  }

private:
  Ref makeEntry(const std::vector<ContentLine> & inCard)
  {
    RefVar entry(AllocateFrame());
    SetFrameSlot(entry, SYMA(class), SYMA(person));
    auto text = [](const ContentLine & inLine) { return UnescapeText(inLine.value); };
    auto set = [&](RefArg inFrame, const char * inSlot, const std::string & inText) {
      if (!inText.empty())
        SetFrameSlot(inFrame, MakeSymbol(inSlot), RefVar(MakeStringFromUTF8(inText)));
    };
    // labels of grouped lines ("item1.X-ABLabel")
    std::map<std::string, std::string> labels;
    for (const ContentLine & line : inCard)
      if (line.name == "X-ABLABEL" && !line.group.empty())
        labels[line.group] = text(line);

    RefVar name(AllocateFrame());
    SetFrameSlot(name, SYMA(class), SYMA(person));
    std::string first, last, company, formatted;
    RefVar addresses(MakeArray(0)), phones(MakeArray(0)), pagers(MakeArray(0)), emails(MakeArray(0)),
           urls(MakeArray(0)), relations(MakeArray(0));
    bool haveAddress = false, haveEmail = false;
    for (const ContentLine & line : inCard) {
      const std::string & n = line.name;
      if (n == "N") {
        std::vector<std::string> parts = SplitValue(line.value, ';');
        parts.resize(5);
        last = parts[0];
        first = parts[1];
        set(name, "first", first);
        set(name, "last", last);
        set(name, "honorific", parts[3]);
        set(name, "title", parts[4]);
      } else if (n == "FN") {
        formatted = text(line);
      } else if (n == "ORG") {
        company = SplitValue(line.value, ';')[0];
        set(entry, "company", company);
      } else if (n == "TITLE") {
        set(entry, "title", text(line));
      } else if (n == "ADR") {
        if (!haveAddress) {
          SetAddress(entry, line.value);
          haveAddress = true;
        } else {
          RefVar more(AllocateFrame());
          SetAddress(more, line.value);
          AddArraySlot(addresses, more);
        }
      } else if (n == "TEL") {
        RefVar number(MakeStringFromUTF8(text(line)));
        if (line.hasType("PAGER")) {
          RefVar pager(AllocateFrame());
          SetFrameSlot(pager, MakeSymbol("pagerNum"), number);
          AddArraySlot(pagers, pager);
          continue;
        }
        const char * phoneClass = "phone";
        if (line.hasType("FAX"))
          phoneClass = line.hasType("HOME") ? "homefaxPhone" : "faxPhone";
        else if (line.hasType("CAR"))
          phoneClass = "carPhone";
        else if (line.hasType("CELL"))
          phoneClass = "mobilePhone";
        else if (line.hasType("WORK"))
          phoneClass = "workPhone";
        else if (line.hasType("HOME"))
          phoneClass = "homePhone";
        SetClass(number, MakeSymbol(phoneClass));
        AddArraySlot(phones, number);
      } else if (n == "EMAIL") {
        if (!haveEmail) {
          set(entry, "email", text(line));
          haveEmail = true;
        } else {
          RefVar more(AllocateFrame());
          set(more, "email", text(line));
          AddArraySlot(emails, more);
        }
      } else if (n == "URL") {
        AddArraySlot(urls, RefVar(MakeStringFromUTF8(text(line))));
      } else if (n == "BDAY") {
        int64_t date = NewtonFromVCardDate(line.value);
        if (date >= 0)
          SetFrameSlot(entry, MakeSymbol("bday"), MAKEINT(date));
      } else if (n == "ANNIVERSARY" || (n == "X-ABDATE" && Lower(LabelText(labels[line.group])) == "anniversary")) {
        int64_t date = NewtonFromVCardDate(line.value);
        if (date >= 0)
          SetFrameSlot(entry, MakeSymbol("anniversary"), MAKEINT(date));
      } else if (n == "X-ABRELATEDNAMES" || n == "RELATED") {
        std::string other = text(line);
        RefVar person(AllocateFrame());
        SetFrameSlot(person, SYMA(class), SYMA(person));
        std::string label = LabelText(line.group.empty() ? line.param("TYPE") : labels[line.group]);
        set(person, "title", label);
        size_t blank = other.find(' ');
        set(person, "first", other.substr(0, blank));
        if (blank != std::string::npos)
          set(person, "last", other.substr(blank + 1));
        AddArraySlot(relations, person);
      } else if (n == "NOTE") {
        RefVar notes(MakeArray(1));
        SetArraySlot(notes, 0, RefVar(MakeParagraph(text(line), 10, 10, 310, 100, mContext.userFont)));
        SetFrameSlot(entry, MakeSymbol("notes"), notes);
      }
    }
    if (first.empty() && last.empty() && company.empty() && !formatted.empty()) {
      // only a formatted name: its last word is the last name
      size_t blank = formatted.rfind(' ');
      first = blank == std::string::npos ? formatted : formatted.substr(0, blank);
      last = blank == std::string::npos ? std::string() : formatted.substr(blank + 1);
      set(name, "first", first);
      set(name, "last", last);
    }
    SetFrameSlot(entry, SYMA(name), name);

    // sorted by "last first", or the company's name
    std::string sortOn;
    if (!first.empty() || !last.empty())
      sortOn = first.empty() ? last : last.empty() ? first : last + " " + first;
    else {
      SetFrameSlot(entry, SYMA(class), SYMA(company));
      sortOn = company;
    }
    RefVar sorton(MakeStringFromUTF8(sortOn));
    SetClass(sorton, SYMA(name));
    SetFrameSlot(entry, MakeSymbol("sorton"), sorton);

    if (Length(addresses) > 0)
      SetFrameSlot(entry, MakeSymbol("addresses"), addresses);
    if (Length(phones) > 0)
      SetFrameSlot(entry, MakeSymbol("phones"), phones);
    if (Length(pagers) > 0)
      SetFrameSlot(entry, MakeSymbol("pagers"), pagers);
    if (Length(emails) > 0)
      SetFrameSlot(entry, MakeSymbol("emailAddrs"), emails);
    if (Length(urls) > 0)
      SetFrameSlot(entry, MakeSymbol("URLs"), urls);
    if (Length(relations) > 0)
      SetFrameSlot(entry, MakeSymbol("names"), relations);
    SetFrameSlot(entry, SYMA(labels), RA(NILREF));
    SetFrameSlot(entry, SYMA(version), MAKEINT(2));
    SetFrameSlot(entry, MakeSymbol("cardType"), MAKEINT(1));
    return entry;
  }

  std::vector<std::vector<ContentLine>> mCards;
  size_t mNext = 0;
};

} // namespace


std::unique_ptr<Exporter> MakeNameToVCard() { return std::make_unique<NameToVCard>(); }
std::unique_ptr<Importer> MakeNameFromVCard() { return std::make_unique<NameFromVCard>(); }

} // namespace ncx
