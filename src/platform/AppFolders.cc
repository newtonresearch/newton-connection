/*
 File: AppFolders.cc

 Where NCX keeps its files (AppFolders.h).
 */

#include "platform/AppFolders.h"

#include <cstdlib>
#include <system_error>

#if defined(_WIN32)
#include <cstring>
#include <cwchar>
#define WIN32_LEAN_AND_MEAN
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace ncx {

namespace {

/** An environment variable as a path; empty if it isn't set. */
fs::path EnvPath(const char * inName)
{
#if defined(_WIN32)
  std::wstring name(inName, inName + std::strlen(inName));
  const wchar_t * value = _wgetenv(name.c_str());
  return (value && *value) ? fs::path(value) : fs::path();
#else
  const char * value = std::getenv(inName);
  return (value && *value) ? fs::path(value) : fs::path();
#endif
}

fs::path Made(const fs::path & inFolder)
{
  if (!inFolder.empty()) {
    std::error_code error;
    fs::create_directories(inFolder, error);
  }
  return inFolder;
}

} // namespace


fs::path AppDataFolder()
{
  fs::path folder = EnvPath("NCX_DATA_DIR");
  if (folder.empty()) {
#if defined(_WIN32)
    fs::path base = EnvPath("APPDATA");
    if (!base.empty())
      folder = base / "Newton Connection";
#elif defined(__APPLE__)
    fs::path home = EnvPath("HOME");
    if (!home.empty())
      folder = home / "Library" / "Application Support" / "Newton Connection";
#else
    fs::path base = EnvPath("XDG_DATA_HOME");
    if (base.empty() && !EnvPath("HOME").empty())
      base = EnvPath("HOME") / ".local" / "share";
    if (!base.empty())
      folder = base / "newton-connection";
#endif
  }
  return Made(folder);
}


fs::path AppDataFile(const std::string & inName)
{
  fs::path folder = AppDataFolder();
  return folder.empty() ? fs::path() : folder / PathFromUTF8(inName);
}


fs::path LogFilePath()
{
  fs::path file = EnvPath("NCX_LOG_FILE");
  if (file.empty()) {
#if defined(_WIN32)
    fs::path base = EnvPath("LOCALAPPDATA");
    if (!base.empty())
      file = base / "Newton Connection" / "NewtonConnection.log";
#elif defined(__APPLE__)
    fs::path home = EnvPath("HOME");
    if (!home.empty())
      file = home / "Library" / "Logs" / "NewtonConnection.log";
#else
    fs::path base = EnvPath("XDG_STATE_HOME");
    if (base.empty() && !EnvPath("HOME").empty())
      base = EnvPath("HOME") / ".local" / "state";
    if (!base.empty())
      file = base / "newton-connection" / "NewtonConnection.log";
#endif
  }
  if (!file.empty())
    Made(file.parent_path());
  return file;
}


fs::path HomeFolder()
{
#if defined(_WIN32)
  fs::path home = EnvPath("USERPROFILE");
#else
  fs::path home = EnvPath("HOME");
#endif
  return home.empty() ? fs::path(".") : home;
}


std::string ComputerName()
{
#if defined(_WIN32)
  wchar_t name[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD length = MAX_COMPUTERNAME_LENGTH + 1;
  if (GetComputerNameW(name, &length))
    return UTF8FromPath(fs::path(std::wstring(name, length)));
#else
  char name[256];
  if (gethostname(name, sizeof(name)) == 0) {
    name[sizeof(name) - 1] = 0;
    std::string text = name;
    // "Matts-Mac.local": the name without the domain
    size_t dot = text.find('.');
    if (dot != std::string::npos && dot > 0)
      text.erase(dot);
    return text;
  }
#endif
  return "Desktop";
}


fs::path PathFromUTF8(const std::string & inText)
{
  return fs::u8path(inText);
}


std::string UTF8FromPath(const fs::path & inPath)
{
  return inPath.u8string();
}

} // namespace ncx
