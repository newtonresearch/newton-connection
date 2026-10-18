/*
 File: Preferences.cc

 The preferences window (Preferences.h).
 */

#include "ui/Preferences.h"
#include "ui/App.h"
#include "ui/Format.h"

#include "base/Prefs.h"
#include "platform/AppFolders.h"

#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Secret_Input.H>

#include <cstdlib>

namespace ncx {

namespace {

const int kTCPDefault = 3679;

/** "/dev/cu.usbserial" as a menu label ('/' and '&' are menu syntax). */
std::string MenuLabel(const std::string & inText)
{
  std::string label;
  for (char c : inText) {
    if (c == '/' || c == '\\' || c == '&' || c == '_')
      label += '\\';
    label += c;
  }
  return label;
}

} // namespace


const std::vector<int> & SerialSpeeds()
{
  static const std::vector<int> speeds = { 38400, 57600, 115200, 230400 };
  return speeds;
}


PreferencesWindow & PreferencesWindow::Show()
{
  static PreferencesWindow * window = nullptr;
  if (!window) {
    Fl_Group::current(nullptr);
    window = new PreferencesWindow();
  }
  window->load();
  window->show();
  return *window;
}


PreferencesWindow::PreferencesWindow()
  : PreferencesUI(0, 0, 480, 310, "Preferences")
{
  mLogLevel->add("None");
  mLogLevel->add("Minimum");
  mLogLevel->add("Full");
  mLogLevel->add("Developer");
  for (int speed : SerialSpeeds()) {
    std::string label = GroupedNumber(speed) + (speed == 38400 ? " (Default)" : "");
    mBaud->add(MenuLabel(label).c_str());
  }
  mLogPath->copy_label(UTF8FromPath(LogFilePath()).c_str());
  mOK->callback(OKCB, this);
  mCancel->callback(CancelCB, this);
  mCancel->shortcut(FL_Escape);
  mRefresh->callback(RefreshCB, this);
  callback(CancelCB, this);           // the close box
}


void PreferencesWindow::listSerialPorts(const std::string & inSelected)
{
  mPorts = ListSerialPorts();
  mSerialPort->clear();
  mSerialPort->add("None");
  int selected = 0;
  for (size_t i = 0; i < mPorts.size(); ++i) {
    mSerialPort->add(MenuLabel(mPorts[i].name.empty() ? mPorts[i].path : mPorts[i].name).c_str());
    if (mPorts[i].path == inSelected)
      selected = (int)i + 1;
  }
  // one set that isn't there now (unplugged): kept
  if (selected == 0 && !inSelected.empty()) {
    mPorts.push_back({ inSelected, inSelected + " (not found)" });
    mSerialPort->add(MenuLabel(mPorts.back().name).c_str());
    selected = (int)mPorts.size();
  }
  mSerialPort->value(selected);
}


void PreferencesWindow::load()
{
  Prefs & prefs = Prefs::App();
  mSetTime->value(prefs.getBool(pref::kSetNewtonTime));
  mAutoBackup->value(prefs.getBool(pref::kAutoBackup));
  mPassword->value(prefs.getString(pref::kPassword).c_str());
  mLogLevel->value(prefs.logLevel());
  mLogToFile->value(prefs.logToFile());
  mTCPPort->value(std::to_string(prefs.tcpPort()).c_str());
  listSerialPorts(prefs.getString(pref::kSerialPort));
  int baud = prefs.baudRate(), index = 0;
  for (size_t i = 0; i < SerialSpeeds().size(); ++i)
    if (SerialSpeeds()[i] == baud)
      index = (int)i;
  mBaud->value(index);
}


void PreferencesWindow::save()
{
  Prefs & prefs = Prefs::App();
  prefs.setBool(pref::kSetNewtonTime, mSetTime->value() != 0);
  prefs.setBool(pref::kAutoBackup, mAutoBackup->value() != 0);
  prefs.setString(pref::kPassword, mPassword->value());
  prefs.setInt(pref::kLogLevel, mLogLevel->value());
  prefs.setBool(pref::kLogToFile, mLogToFile->value() != 0);
  int port = std::atoi(mTCPPort->value());
  prefs.setInt(pref::kTCPIPPort, port > 0 && port < 65536 ? port : kTCPDefault);
  int serial = mSerialPort->value();
  prefs.setString(pref::kSerialPort, serial > 0 && serial <= (int)mPorts.size() ? mPorts[serial - 1].path : std::string());
  int baud = mBaud->value();
  prefs.setInt(pref::kSerialBaud, SerialSpeeds()[baud >= 0 && baud < (int)SerialSpeeds().size() ? baud : 0]);
  prefs.flush();
}


void PreferencesWindow::OKCB(Fl_Widget *, void * inWindow)
{
  PreferencesWindow * window = static_cast<PreferencesWindow *>(inWindow);
  window->save();
  window->hide();
  App::get().preferencesChanged();
}


void PreferencesWindow::CancelCB(Fl_Widget *, void * inWindow)
{
  static_cast<PreferencesWindow *>(inWindow)->hide();
}


void PreferencesWindow::RefreshCB(Fl_Widget *, void * inWindow)
{
  PreferencesWindow * window = static_cast<PreferencesWindow *>(inWindow);
  int selected = window->mSerialPort->value();
  std::string path = selected > 0 && selected <= (int)window->mPorts.size() ? window->mPorts[selected - 1].path : std::string();
  window->listSerialPorts(path);
}

} // namespace ncx
