/*
 File: Preferences.h

 The preferences window (PORTING.md 7.6; the Cocoa app's General and
 Serial panes): General (set the Newton's date and time when it connects,
 back up on auto dock, the Newton's password, logging), Connection (the
 TCP/IP port, the serial port and its speed). OK writes them (Prefs.h) and
 the app takes them up (logging at once, where it listens once no Newton
 is docked). Its layout: PreferencesUI.fl. One for the app.
 */

#ifndef NCX_UI_PREFERENCES_H
#define NCX_UI_PREFERENCES_H

#include "PreferencesUI.h"

#include "platform/SerialPorts.h"

#include <vector>

namespace ncx {

class PreferencesWindow : public PreferencesUI
{
public:
  /** The window, shown (made the first time). */
  static PreferencesWindow & Show();

  /** The fields from the preferences, and back. */
  void load();
  void save();

private:
  PreferencesWindow();
  void listSerialPorts(const std::string & inSelected);
  static void OKCB(Fl_Widget *, void *);
  static void CancelCB(Fl_Widget *, void *);
  static void RefreshCB(Fl_Widget *, void *);

  std::vector<SerialPort> mPorts;
};

/** The serial speeds offered, bps. */
const std::vector<int> & SerialSpeeds();

} // namespace ncx

#endif // NCX_UI_PREFERENCES_H
