/*
 File: DeviceList.cc

 The device files in a folder (DeviceList.h).
 */

#include "model/DeviceList.h"
#include "base/Log.h"
#include "platform/AppFolders.h"

#include <algorithm>
#include <system_error>

namespace ncx {

const char * const kDeviceFileExtension = ".newtondevice";


std::vector<DeviceFileInfo> ListDeviceFiles(const std::filesystem::path & inFolder)
{
  std::vector<DeviceFileInfo> list;
  std::error_code ec;
  for (const auto & item : std::filesystem::directory_iterator(inFolder, ec)) {
    if (item.path().extension() != kDeviceFileExtension || !item.is_regular_file(ec))
      continue;
    std::string error;
    std::unique_ptr<DeviceFile> file = DeviceFile::Open(item.path(), DeviceFile::kReadOnly, error);
    if (!file) {
      Log(kLogMinimum, "devices: %s left out: %s", UTF8FromPath(item.path().filename()).c_str(), error.c_str());
      continue;
    }
    list.push_back({ item.path(), file->newtonId(), file->newtonName() });
  }
  std::sort(list.begin(), list.end(), [](const DeviceFileInfo & a, const DeviceFileInfo & b) {
    return a.path.filename() < b.path.filename();
  });
  return list;
}


std::filesystem::path FindDeviceFile(const std::filesystem::path & inFolder, const std::string & inNewtonId)
{
  for (const DeviceFileInfo & info : ListDeviceFiles(inFolder))
    if (info.newtonId == inNewtonId)
      return info.path;
  return std::filesystem::path();
}


DeviceRecord SetDevice(DeviceFile & ioFile, const std::string & inName, const NewtonInfo & inInfo)
{
  std::optional<DeviceRecord> found = ioFile.device();
  DeviceRecord device = found ? *found : DeviceRecord();
  device.name = inName;
  SetNewtonInfo(device, inInfo);
  if (found) {
    ioFile.update(device);
    return device;
  }
  // a new device has its UserInfo (the model requires one)
  UserInfoRecord user;
  ioFile.insert(user);
  device.userPk = user.pk;
  ioFile.insert(device);
  user.devicePk = device.pk;
  ioFile.update(user);
  return device;
}


std::unique_ptr<DeviceFile> OpenDeviceFileFor(const std::filesystem::path & inFolder, const NewtonInfo & inInfo,
                                              const std::string & inName, std::string & outError)
{
  std::string newtonId = DeviceId(inInfo);
  std::filesystem::path path = FindDeviceFile(inFolder, newtonId);
  std::unique_ptr<DeviceFile> file;
  if (!path.empty()) {
    file = DeviceFile::Open(path, DeviceFile::kReadWrite, outError);
  } else {
    std::error_code ec;
    std::filesystem::create_directories(inFolder, ec);
    path = inFolder / PathFromUTF8(newtonId + kDeviceFileExtension);
    file = DeviceFile::Create(path, newtonId, inName, outError);
  }
  if (!file)
    return nullptr;
  try {
    DeviceFile::Transaction transaction(*file);
    SetDevice(*file, inName, inInfo);
    if (file->newtonName() != inName)
      file->setIdentity(newtonId, inName);
    transaction.commit();
  } catch (const ModelError & e) {
    outError = e.what();
    return nullptr;
  }
  return file;
}

} // namespace ncx
