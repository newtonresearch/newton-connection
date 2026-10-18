/*
 File: Drop.cc

 Files dropped on the app's windows (Drop.h).
 */

#include "ui/Drop.h"

#include "platform/AppFolders.h"

namespace ncx {

namespace {

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

} // namespace ncx
