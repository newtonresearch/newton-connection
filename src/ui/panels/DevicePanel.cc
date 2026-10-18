/*
 File: DevicePanel.cc

 The device panel (DevicePanel.h).
 */

#include "ui/panels/DevicePanel.h"
#include "ui/App.h"
#include "ui/DocumentWindow.h"
#include "ui/Drop.h"
#include "ui/Format.h"
#include "ui/Images.h"

#include "base/Dates.h"
#include "dock/Packages.h"
#include "model/DeviceFile.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>

#include <cstdint>

namespace ncx {

namespace {

enum Row { kManufacturerRow, kSerialRow, kVersionRow };

const char * PictureName(const NewtonInfo & inInfo)
{
  std::string type = MachineTypeName(inInfo);
  if (type == "MessagePad 130")  return "device-mp130";
  if (type == "MessagePad 2000") return "device-mp2000";
  if (type == "eMate")           return "device-emate";
  return "device-mp100";
}

bool HasSerialNumber(const NewtonInfo & inInfo)
{
  return SerialNumberText(inInfo) != "0000-0000-0000-0000";
}

} // namespace


std::string BackupStatusText(const std::optional<double> & inBackupDate)
{
  if (!inBackupDate)
    return "This Newton device has never been backed up to this computer.";
  int64_t when = TimeFromCoreData(*inBackupDate);
  CivilTime then = LocalTime(when), now = LocalTime(Now());
  if (then.year == now.year && then.month == now.month && then.day == now.day)
    return "This Newton device was backed up today at " + FormatTime(when) + ".";
  return "This Newton device was backed up on " + FormatDate(when) + " at " + FormatTime(when) + ".";
}


DevicePanel::DevicePanel(int inX, int inY, int inW, int inH)
  : DevicePanelUI(inX, inY, inW, inH)
{
  resizable(nullptr);       // the layout stays as it is (Fl_Group's would stretch it)
  box(FL_NO_BOX);
  mManufacturerLabel->callback(LabelCB, (void *)(intptr_t)kManufacturerRow);
  mSerialLabel->callback(LabelCB, (void *)(intptr_t)kSerialRow);
  mVersionLabel->callback(LabelCB, (void *)(intptr_t)kVersionRow);
  for (Fl_Button * label : { mManufacturerLabel, mSerialLabel, mVersionLabel })
    label->clear_visible_focus();
  mBackUp->callback(ButtonCB, (void *)(intptr_t)Command::kBackUp);
  mRestore->callback(ButtonCB, (void *)(intptr_t)Command::kRestore);
  for (Fl_Box * value : { mManufacturer, mType, mSerial, mVersion, mRAM, mProcessor })
    value->labelfont(FL_HELVETICA_BOLD);
}


void DevicePanel::update(DeviceFile & inFile)
{
  mDevice = inFile.device();
  mInfo = mDevice ? NewtonInfoOf(*mDevice) : NewtonInfo();
  mSerialRow = HasSerialNumber(mInfo) ? 1 : 0;
  mVersionRow = 1;
  mManufacturerRow = 0;
  mPicture->image(BuiltinImage(PictureName(mInfo)));
  mType->copy_label(mDevice ? MachineTypeName(mInfo).c_str() : "");
  mRAM->copy_label(mDevice && !Is1xData(mInfo) ? (GroupedNumber(mInfo.fRAMSize / 1024) + "K").c_str() : "");
  mProcessor->copy_label(mDevice && mDevice->processor ? mDevice->processor->c_str() : "");
  mBackupStatus->copy_label(BackupStatusText(mDevice ? mDevice->backupDate : std::nullopt).c_str());
  showRows();
  updateState();
  redraw();
}


void DevicePanel::showRows()
{
  mManufacturerLabel->label(mManufacturerRow == 0 ? "Manufacturer:" : "Manufactured:");
  mManufacturer->copy_label(manufacturerText().c_str());
  mSerialLabel->label(mSerialRow == 0 ? "ID:" : "Serial Number:");
  mSerial->copy_label(serialText().c_str());
  mVersionLabel->label(mVersionRow == 0 ? "ROM Version:" : "OS Version:");
  mVersion->copy_label(versionText().c_str());
  mDeviceBox->redraw();
}


std::string DevicePanel::manufacturerText() const
{
  if (!mDevice)
    return std::string();
  if (mManufacturerRow == 1 && mDevice->manufactureDate)
    return FormatDate(TimeFromCoreData(*mDevice->manufactureDate));
  return ManufacturerName(mInfo);
}


std::string DevicePanel::serialText() const
{
  if (!mDevice)
    return std::string();
  return mSerialRow == 1 ? SerialNumberText(mInfo) : std::to_string(mInfo.fNewtonID);
}


std::string DevicePanel::versionText() const
{
  if (!mDevice)
    return std::string();
  if (mVersionRow == 0)
    return ROMVersionText(mInfo);
  if (mDevice->osInfo && mDevice->osInfo->size() > 2)
    return *mDevice->osInfo;
  return std::to_string(mInfo.fNOSVersion);
}


std::string DevicePanel::backupText() const
{
  return mBackupStatus->label() ? mBackupStatus->label() : "";
}


void DevicePanel::LabelCB(Fl_Widget * inLabel, void * inRow)
{
  DevicePanel * panel = static_cast<DevicePanel *>(inLabel->parent()->parent());
  if (!panel->mDevice)
    return;
  switch ((Row)(intptr_t)inRow) {
    case kManufacturerRow:
      if (panel->mDevice->manufactureDate)
        panel->mManufacturerRow ^= 1;
      break;
    case kSerialRow:
      if (HasSerialNumber(panel->mInfo))
        panel->mSerialRow ^= 1;
      break;
    case kVersionRow:
      if (!Is1xData(panel->mInfo))
        panel->mVersionRow ^= 1;
      break;
  }
  panel->showRows();
}


/*-- Back Up, Restore, packages dropped ---------------------------------------*/

void DevicePanel::updateState()
{
  App & app = App::get();
  DocumentWindow * target = dynamic_cast<DocumentWindow *>(window());
  if (app.canDo(Command::kBackUp, target))
    mBackUp->activate();
  else
    mBackUp->deactivate();
  if (app.canDo(Command::kRestore, target))
    mRestore->activate();
  else
    mRestore->deactivate();
}


void DevicePanel::ButtonCB(Fl_Widget * inButton, void * inCommand)
{
  App::get().command((Command)(intptr_t)inCommand, dynamic_cast<DocumentWindow *>(inButton->window()));
}


bool DevicePanel::acceptsDrops() const
{
  // packages go to the docked Newton, so its window takes them
  DocumentWindow * target = dynamic_cast<DocumentWindow *>(window());
  return App::get().canDo(Command::kInstallPackage, target) && App::get().isDockedWindow(target);
}


int DevicePanel::handle(int inEvent)
{
  switch (inEvent) {
    case FL_DND_ENTER:
    case FL_DND_DRAG:
      mDropping = acceptsDrops();
      return mDropping ? 1 : DevicePanelUI::handle(inEvent);
    case FL_DND_LEAVE:
      mDropping = false;
      return 1;
    case FL_DND_RELEASE:
      return mDropping ? 1 : 0;
    case FL_PASTE:
      if (mDropping) {
        mDropping = false;
        std::vector<std::filesystem::path> packages;
        for (const std::filesystem::path & file : DroppedFiles(Fl::event_text() ? Fl::event_text() : ""))
          if (IsNewtonPackage(file))
            packages.push_back(file);
        if (!packages.empty())
          App::get().installPackages(packages);
        return 1;
      }
      break;
  }
  return DevicePanelUI::handle(inEvent);
}

} // namespace ncx
