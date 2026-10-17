/*
 File: ContentLines.h

 The text format vCard (RFC 2426, 6350) and iCalendar (RFC 5545) share:
 "content lines" of the form

   [group.]NAME[;PARAM=value[,value]...]:value

 folded at 75 octets (a line that starts with a space or tab continues the
 one before), with text values escaping \\, \; \, and \n. Replaces what the
 Cocoa app had from libical and Apple's Contacts framework (PORTING.md 6.2).
 */

#ifndef NCX_TRANSLATORS_CONTENTLINES_H
#define NCX_TRANSLATORS_CONTENTLINES_H

#include <string>
#include <utility>
#include <vector>

namespace ncx {

struct ContentLine
{
  std::string group;                                          // "item1", or ""
  std::string name;                                           // upper case
  std::vector<std::pair<std::string, std::string>> params;    // NAME (upper case), value (no quotes)
  std::string value;                                          // as written (escaped)

  ContentLine() { }
  ContentLine(const std::string & inName, const std::string & inValue) : name(inName), value(inValue) { }

  /** The first parameter of that name ("" if none). */
  std::string param(const std::string & inName) const;
  /** A TYPE (any of a TYPE=a,b list, or vCard 2.1's bare parameters),
      without case. */
  bool hasType(const std::string & inType) const;
  ContentLine & addParam(const std::string & inName, const std::string & inValue)
  {
    params.emplace_back(inName, inValue);
    return *this;
  }
};

/** The lines of a vCard or iCalendar text: unfolded, CR LF or LF. Lines
    without a ':' are left out. */
std::vector<ContentLine> ParseContentLines(const std::string & inText);

/** One line, folded at 75 octets (not inside a UTF-8 character), ending
    in CR LF. */
std::string FormatContentLine(const ContentLine & inLine);

/** A text value escaped: \\ \; \, and a line end as \n. */
std::string EscapeText(const std::string & inText);
/** And back (\n and \N are LF). */
std::string UnescapeText(const std::string & inText);

/** A structured value (N, ADR, RRULE parts) split at the separators that
    aren't escaped; each part unescaped. */
std::vector<std::string> SplitValue(const std::string & inValue, char inSeparator);

/** Upper case (ASCII). */
std::string Upper(std::string inText);

} // namespace ncx

#endif // NCX_TRANSLATORS_CONTENTLINES_H
