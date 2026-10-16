/*
 File: SerialPorts.cc

 The serial ports there are (SerialPorts.h).
 */

#include "platform/SerialPorts.h"

#include <algorithm>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <filesystem>
#include <map>
#include <system_error>
#endif

namespace ncx {

namespace {

#if defined(__APPLE__)

std::string StringProperty(io_object_t inService, CFStringRef inKey)
{
  std::string text;
  CFTypeRef value = IORegistryEntryCreateCFProperty(inService, inKey, kCFAllocatorDefault, 0);
  if (value && CFGetTypeID(value) == CFStringGetTypeID()) {
    char buffer[1024];
    if (CFStringGetCString((CFStringRef)value, buffer, sizeof(buffer), kCFStringEncodingUTF8))
      text = buffer;
  }
  if (value)
    CFRelease(value);
  return text;
}

#elif defined(_WIN32)

std::string UTF8(const wchar_t * inText)
{
  int size = WideCharToMultiByte(CP_UTF8, 0, inText, -1, nullptr, 0, nullptr, nullptr);
  std::string text(size > 0 ? (size_t)size - 1 : 0, '\0');
  if (size > 1)
    WideCharToMultiByte(CP_UTF8, 0, inText, -1, &text[0], size, nullptr, nullptr);
  return text;
}

#endif

} // namespace


std::vector<SerialPort> ListSerialPorts()
{
  std::vector<SerialPort> ports;

#if defined(__APPLE__)
  CFMutableDictionaryRef matching = IOServiceMatching(kIOSerialBSDServiceValue);
  if (matching) {
    CFDictionarySetValue(matching, CFSTR(kIOSerialBSDTypeKey), CFSTR(kIOSerialBSDAllTypes));
    io_iterator_t iterator = 0;
    // MACH_PORT_NULL: the default main port (kIOMasterPortDefault, by any name)
    if (IOServiceGetMatchingServices(MACH_PORT_NULL, matching, &iterator) == KERN_SUCCESS) {
      while (io_object_t service = IOIteratorNext(iterator)) {
        SerialPort port;
        port.path = StringProperty(service, CFSTR(kIOCalloutDeviceKey));
        port.name = StringProperty(service, CFSTR(kIOTTYDeviceKey));
        if (!port.path.empty())
          ports.push_back(port);
        IOObjectRelease(service);
      }
      IOObjectRelease(iterator);
    }
  }

#elif defined(_WIN32)
  HKEY key;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key) == ERROR_SUCCESS) {
    for (DWORD i = 0; ; ++i) {
      wchar_t name[256], data[256];
      DWORD nameSize = 256, dataSize = sizeof(data), type = 0;
      LONG result = RegEnumValueW(key, i, name, &nameSize, nullptr, &type, (LPBYTE)data, &dataSize);
      if (result == ERROR_NO_MORE_ITEMS)
        break;
      if (result != ERROR_SUCCESS || type != REG_SZ)
        continue;
      data[255] = 0;
      SerialPort port;
      port.path = UTF8(data);                 // COM3
      port.name = port.path + " (" + UTF8(name) + ")";
      ports.push_back(port);
    }
    RegCloseKey(key);
  }

#else
  namespace fs = std::filesystem;
  std::error_code error;
  // the names under /dev/serial/by-id, by the device they lead to
  std::map<std::string, std::string> names;
  for (const fs::directory_entry & link : fs::directory_iterator("/dev/serial/by-id", error)) {
    fs::path target = fs::canonical(link.path(), error);
    if (!error)
      names[target.string()] = link.path().filename().string();
  }
  // tty devices with hardware behind them (not the virtual consoles; the
  // 8250 driver's ports have a device even when there is no UART: only those
  // that have a name or are USB/ACM stay)
  for (const fs::directory_entry & tty : fs::directory_iterator("/sys/class/tty", error)) {
    std::string name = tty.path().filename().string();
    if (!fs::exists(tty.path() / "device", error))
      continue;
    std::string path = "/dev/" + name;
    bool usb = name.compare(0, 6, "ttyUSB") == 0 || name.compare(0, 6, "ttyACM") == 0;
    auto known = names.find(path);
    if (!usb && known == names.end() && name.compare(0, 4, "ttyS") == 0)
      continue;
    SerialPort port;
    port.path = path;
    port.name = known != names.end() ? known->second : name;
    ports.push_back(port);
  }
#endif

  std::sort(ports.begin(), ports.end(), [](const SerialPort & a, const SerialPort & b) { return a.path < b.path; });
  return ports;
}

} // namespace ncx
