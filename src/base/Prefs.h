/*
 File: Prefs.h

 NCX's preferences (PORTING.md 2.4): Fl_Preferences underneath (a file per
 user: on macOS ~/Library/Preferences/newtonresearch.org/Newton
 Connection.prefs), with the Cocoa app's key names (PreferenceKeys.h). The
 old NSUserDefaults aren't read. Any thread may use them (the dock thread
 reads the password, the port, ...).

 Unset values read as the default given; the accessors at the end have the
 Cocoa app's fallbacks built in (TCP port 3679, 38400 bps).
 */

#ifndef NCX_BASE_PREFS_H
#define NCX_BASE_PREFS_H

#include <memory>
#include <mutex>
#include <string>

namespace ncx {

namespace pref {
// Application
inline constexpr const char * kNewton1SessionType = "Newton1SessionType";
inline constexpr const char * kNoDeleteWarning    = "DontShowDeleteWarning";
inline constexpr const char * kNoAddWarning       = "DontShowAddWarning";
inline constexpr const char * kNoSyncWarning      = "DontShowSyncWarning";
inline constexpr const char * kUserFontName       = "UserFontName";
inline constexpr const char * kUserFontSize       = "UserFontSize";
// General
inline constexpr const char * kSetNewtonTime      = "SetNewtonTime";
inline constexpr const char * kAutoBackup         = "BackUpOnAutoDock";
inline constexpr const char * kSelectiveBackup    = "SelectiveBackup";
inline constexpr const char * kAutoSync           = "SyncOnAutoDock";
// Serial
inline constexpr const char * kTCPIPPort          = "TCPIPPort";
inline constexpr const char * kSerialPort         = "SerialPort";
inline constexpr const char * kSerialBaud         = "BaudRate";
// Security
inline constexpr const char * kPassword           = "Password";
// ROM dump
inline constexpr const char * kROMAddress         = "ROMAddress";
inline constexpr const char * kROMSize            = "ROMSize";
// Debug
inline constexpr const char * kLogToFile          = "LogToFile";
inline constexpr const char * kLogLevel           = "LogLevel";
} // namespace pref

class Prefs
{
public:
  /** The user's preferences (opened the first time). */
  static Prefs & App();

  /** Preferences only in memory, gone at the end (tests). inGroup keeps
      several apart. */
  static std::unique_ptr<Prefs> InMemory(const std::string & inGroup);

  ~Prefs();

  bool has(const char * inKey);
  void remove(const char * inKey);

  bool getBool(const char * inKey, bool inDefault = false);
  int getInt(const char * inKey, int inDefault = 0);
  double getDouble(const char * inKey, double inDefault = 0.0);
  std::string getString(const char * inKey, const std::string & inDefault = std::string());

  void setBool(const char * inKey, bool inValue);
  void setInt(const char * inKey, int inValue);
  void setDouble(const char * inKey, double inValue);
  void setString(const char * inKey, const std::string & inValue);

  /** Write them to their file now (they are written at the end anyway). */
  void flush();

  // with the Cocoa app's fallbacks
  int tcpPort();          // TCPIPPort, 3679 (kNewtonDockServicePort) if unset or 0
  int baudRate();         // BaudRate, 38400 if unset or 0
  int logLevel();         // LogLevel, 0 ... 3
  bool logToFile();       // LogToFile

private:
  struct Store;
  explicit Prefs(std::unique_ptr<Store> inStore);
  std::unique_ptr<Store> mStore;
  std::mutex mLock;
};

} // namespace ncx

#endif // NCX_BASE_PREFS_H
