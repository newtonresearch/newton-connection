/*
 File: DeviceList.h

 The .newtondevice files in the app's folder (PORTING.md 4.3): which Newton
 each one backs up (Z_METADATA's NewtonId and NewtonName), and the file for
 a Newton that connects (NCDocument setDevice:info:). No Newton objects.
 */

#ifndef NCX_MODEL_DEVICELIST_H
#define NCX_MODEL_DEVICELIST_H

#include "model/DeviceFile.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ncx {

/** The extension of device files, ".newtondevice". */
extern const char * const kDeviceFileExtension;

struct DeviceFileInfo
{
  std::filesystem::path path;
  std::string newtonId;                   // DeviceId(): "0000-4E65-7774-6F6E"
  std::string newtonName;                 // the owner's name
};

/** The device files in a folder (read only), by file name. Files that
    aren't device files are left out. */
std::vector<DeviceFileInfo> ListDeviceFiles(const std::filesystem::path & inFolder);

/** The file in the folder for a NewtonId (by Z_METADATA, as the Cocoa app
    looked it up); "" if there is none. */
std::filesystem::path FindDeviceFile(const std::filesystem::path & inFolder, const std::string & inNewtonId);

/** The file's device (with its UserInfo), made if there is none; its name
    and info set (NCDocument setDevice:info:). */
DeviceRecord SetDevice(DeviceFile & ioFile, const std::string & inName, const NewtonInfo & inInfo);

/** The device file for a Newton that connects, opened for writing: the one
    with its NewtonId, or a new "<DeviceId>.newtondevice"; its device set
    from the info and the name, and Z_METADATA too. nullptr and why if the
    file can't be opened or made. */
std::unique_ptr<DeviceFile> OpenDeviceFileFor(const std::filesystem::path & inFolder, const NewtonInfo & inInfo,
                                              const std::string & inName, std::string & outError);

} // namespace ncx

#endif // NCX_MODEL_DEVICELIST_H
