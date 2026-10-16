/*
 File: SerialPorts.h

 The serial ports there are (PORTING.md 3.3), for the preferences: macOS
 through IOKit (the callout devices, /dev/cu.*, as the Cocoa app listed
 them), Linux the tty devices with hardware behind them (/sys/class/tty),
 named by /dev/serial/by-id where there is a name, Windows the registry's
 HARDWARE\DEVICEMAP\SERIALCOMM (COM1, COM3, ...).
 */

#ifndef NCX_PLATFORM_SERIALPORTS_H
#define NCX_PLATFORM_SERIALPORTS_H

#include <string>
#include <vector>

namespace ncx {

struct SerialPort
{
  std::string path;     // what to open: "/dev/cu.usbserial-1420", "COM3"
  std::string name;     // what to show: "usbserial-1420", "usb-FTDI_..."
};

/** Sorted by path. */
std::vector<SerialPort> ListSerialPorts();

} // namespace ncx

#endif // NCX_PLATFORM_SERIALPORTS_H
