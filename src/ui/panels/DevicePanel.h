/*
 File: DevicePanel.h

 The device panel (PORTING.md 7.4; the Cocoa app's NCDeviceViewController):
 the Newton's picture and what it is (three rows show one of two things,
 a click on their label switches: manufacturer or when it was made, serial
 number or Newton ID, OS or ROM version), and its backups: when the last
 one was, Back Up, Restore (RestoreDialog). Packages dropped on it are
 installed on the docked Newton. Its layout: DevicePanelUI.fl.
 */

#ifndef NCX_UI_PANELS_DEVICEPANEL_H
#define NCX_UI_PANELS_DEVICEPANEL_H

#include "DevicePanelUI.h"

#include "model/Records.h"

#include <optional>
#include <string>

namespace ncx {

class DeviceFile;

class DevicePanel : public DevicePanelUI
{
public:
  DevicePanel(int inX, int inY, int inW, int inH);

  /** Show the file's device. */
  void update(DeviceFile & inFile);
  /** The buttons, and drops, as the app allows them now. */
  void updateState();

  int handle(int inEvent) override;

  /** The texts shown (tests). */
  std::string manufacturerText() const;
  std::string serialText() const;
  std::string versionText() const;
  std::string backupText() const;

private:
  void showRows();
  bool acceptsDrops() const;
  static void LabelCB(Fl_Widget *, void *);
  static void ButtonCB(Fl_Widget *, void *);

  std::optional<DeviceRecord> mDevice;
  NewtonInfo mInfo;
  int mManufacturerRow = 0;       // 0 manufacturer, 1 manufactured
  int mSerialRow = 1;             // 0 Newton ID, 1 serial number
  int mVersionRow = 1;            // 0 ROM version, 1 OS version
  bool mDropping = false;
};

/** "This Newton device was backed up today at 3:04 PM", "... on Oct 16,
    2026 at 3:04 PM", "... has never been backed up to this computer." */
std::string BackupStatusText(const std::optional<double> & inBackupDate);

} // namespace ncx

#endif // NCX_UI_PANELS_DEVICEPANEL_H
