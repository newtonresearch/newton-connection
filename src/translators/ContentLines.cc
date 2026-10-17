/*
 File: ContentLines.cc

 vCard and iCalendar content lines (ContentLines.h).
 */

#include "translators/ContentLines.h"

#include <algorithm>
#include <cctype>

namespace ncx {

std::string Upper(std::string inText)
{
  std::transform(inText.begin(), inText.end(), inText.begin(), [](char c) { return (char)std::toupper((unsigned char)c); });
  return inText;
}


std::string ContentLine::param(const std::string & inName) const
{
  for (const auto & p : params)
    if (p.first == inName)
      return p.second;
  return std::string();
}


bool ContentLine::hasType(const std::string & inType) const
{
  std::string wanted = Upper(inType);
  for (const auto & p : params) {
    if (p.first != "TYPE")
      continue;
    for (const std::string & type : SplitValue(p.second, ','))
      if (Upper(type) == wanted)
        return true;
  }
  return false;
}


namespace {

/** One unfolded line into its parts. False if it has no ':'. */
bool ParseLine(const std::string & inLine, ContentLine & outLine)
{
  // the name (and group) up to ';' or ':'
  size_t at = 0;
  while (at < inLine.size() && inLine[at] != ';' && inLine[at] != ':')
    ++at;
  if (at == inLine.size())
    return false;
  std::string name = inLine.substr(0, at);
  size_t dot = name.find('.');
  if (dot != std::string::npos) {
    outLine.group = name.substr(0, dot);
    name = name.substr(dot + 1);
  }
  outLine.name = Upper(name);
  // the parameters, up to the ':' that isn't quoted
  while (at < inLine.size() && inLine[at] == ';') {
    size_t start = ++at;
    bool quoted = false;
    while (at < inLine.size() && (quoted || (inLine[at] != ';' && inLine[at] != ':'))) {
      if (inLine[at] == '"')
        quoted = !quoted;
      ++at;
    }
    std::string param = inLine.substr(start, at - start);
    size_t equal = param.find('=');
    std::string paramName = equal == std::string::npos ? std::string("TYPE") : Upper(param.substr(0, equal));
    std::string value = equal == std::string::npos ? param : param.substr(equal + 1);
    value.erase(std::remove(value.begin(), value.end(), '"'), value.end());
    outLine.params.emplace_back(paramName, value);
  }
  if (at >= inLine.size() || inLine[at] != ':')
    return false;
  outLine.value = inLine.substr(at + 1);
  return true;
}

} // namespace


std::vector<ContentLine> ParseContentLines(const std::string & inText)
{
  // physical lines, then unfolded
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < inText.size()) {
    size_t end = inText.find('\n', start);
    if (end == std::string::npos)
      end = inText.size();
    std::string line = inText.substr(start, end - start);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (!line.empty() && (line[0] == ' ' || line[0] == '\t') && !lines.empty())
      lines.back() += line.substr(1);
    else
      lines.push_back(line);
    start = end + 1;
  }
  std::vector<ContentLine> result;
  for (const std::string & line : lines) {
    ContentLine parsed;
    if (ParseLine(line, parsed))
      result.push_back(std::move(parsed));
  }
  return result;
}


std::string FormatContentLine(const ContentLine & inLine)
{
  std::string line = inLine.group.empty() ? inLine.name : inLine.group + "." + inLine.name;
  for (const auto & p : inLine.params) {
    bool quote = p.second.find_first_of(":;,") != std::string::npos;
    line += ";" + p.first + "=" + (quote ? "\"" + p.second + "\"" : p.second);
  }
  line += ":" + inLine.value;
  // folded: at most 75 octets a line, a continuation starts with a space
  std::string folded;
  size_t lineLength = 0;
  for (size_t i = 0; i < line.size(); ++i) {
    unsigned char c = (unsigned char)line[i];
    bool startsCharacter = (c & 0xC0) != 0x80;
    if (startsCharacter && lineLength >= 75) {
      folded += "\r\n ";
      lineLength = 1;
    }
    folded += (char)c;
    ++lineLength;
  }
  return folded + "\r\n";
}


std::string EscapeText(const std::string & inText)
{
  std::string out;
  for (size_t i = 0; i < inText.size(); ++i) {
    char c = inText[i];
    if (c == '\\' || c == ';' || c == ',') {
      out += '\\';
      out += c;
    } else if (c == '\r') {
      out += "\\n";
      if (i + 1 < inText.size() && inText[i + 1] == '\n')
        ++i;
    } else if (c == '\n') {
      out += "\\n";
    } else {
      out += c;
    }
  }
  return out;
}


std::string UnescapeText(const std::string & inText)
{
  std::string out;
  for (size_t i = 0; i < inText.size(); ++i) {
    char c = inText[i];
    if (c == '\\' && i + 1 < inText.size()) {
      char next = inText[++i];
      out += next == 'n' || next == 'N' ? '\n' : next;
    } else {
      out += c;
    }
  }
  return out;
}


std::vector<std::string> SplitValue(const std::string & inValue, char inSeparator)
{
  std::vector<std::string> parts;
  std::string part;
  for (size_t i = 0; i < inValue.size(); ++i) {
    char c = inValue[i];
    if (c == '\\' && i + 1 < inValue.size()) {
      part += c;
      part += inValue[++i];
    } else if (c == inSeparator) {
      parts.push_back(UnescapeText(part));
      part.clear();
    } else {
      part += c;
    }
  }
  parts.push_back(UnescapeText(part));
  return parts;
}

} // namespace ncx
