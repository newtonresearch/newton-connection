/*
 File: Drop.cc

 Files dropped on the app's windows (Drop.h).
 */

#include "ui/Drop.h"

#include "platform/AppFolders.h"

#include <cstdlib>
#include <cstring>

namespace ncx {

namespace {

const char * const kEntriesTag = "ncx-entries:";

int HexValue(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/** "file:///home/ada/My%20Notes.txt" as "/home/ada/My Notes.txt";
    "file:///C:/..." as "C:/..." (Windows); a path as it is. */
std::string PathOfLine(const std::string & inLine)
{
  if (inLine.compare(0, 7, "file://") != 0)
    return inLine;
  std::string rest = inLine.substr(7);
  // file://host/path: the host is left out (localhost or none)
  size_t slash = rest.find('/');
  if (slash == std::string::npos)
    return std::string();
  rest = rest.substr(slash);
  std::string path;
  for (size_t i = 0; i < rest.size(); ++i) {
    int high, low;
    if (rest[i] == '%' && i + 2 < rest.size() && (high = HexValue(rest[i + 1])) >= 0 && (low = HexValue(rest[i + 2])) >= 0) {
      path += (char)(high * 16 + low);
      i += 2;
    } else
      path += rest[i];
  }
#if defined(_WIN32)
  if (path.size() > 2 && path[0] == '/' && path[2] == ':')
    path.erase(0, 1);
#endif
  return path;
}

} // namespace


std::vector<std::filesystem::path> DroppedFiles(const std::string & inText)
{
  std::vector<std::filesystem::path> files;
  size_t start = 0;
  while (start < inText.size()) {
    size_t end = inText.find_first_of("\r\n", start);
    if (end == std::string::npos)
      end = inText.size();
    std::string line = inText.substr(start, end - start);
    start = end + 1;
    if (line.empty())
      continue;
    std::string text = PathOfLine(line);
    if (text.empty())
      continue;
    std::filesystem::path path = PathFromUTF8(text);
    std::error_code error;
    if (std::filesystem::exists(path, error))
      files.push_back(path);
  }
  return files;
}


std::string EntriesPayload(const std::filesystem::path & inFile, const std::vector<int64_t> & inEntryPks)
{
  std::string text = kEntriesTag + std::to_string(inEntryPks.size()) + "\n" + UTF8FromPath(inFile) + "\n";
  for (size_t i = 0; i < inEntryPks.size(); ++i)
    text += (i ? " " : "") + std::to_string(inEntryPks[i]);
  return text + "\n";
}


bool ParseEntriesPayload(const std::string & inText, std::filesystem::path & outFile, std::vector<int64_t> & outEntryPks)
{
  size_t tag = std::strlen(kEntriesTag);
  if (inText.compare(0, tag, kEntriesTag) != 0)
    return false;
  size_t first = inText.find('\n'), second = first == std::string::npos ? first : inText.find('\n', first + 1);
  if (second == std::string::npos)
    return false;
  size_t count = std::strtoul(inText.c_str() + tag, nullptr, 10);
  outFile = PathFromUTF8(inText.substr(first + 1, second - first - 1));
  outEntryPks.clear();
  const char * p = inText.c_str() + second + 1;
  while (*p) {
    char * end = nullptr;
    long long pk = std::strtoll(p, &end, 10);
    if (end == p)
      break;
    outEntryPks.push_back(pk);
    p = end;
  }
  return !outFile.empty() && outEntryPks.size() == count && count > 0;
}

} // namespace ncx
