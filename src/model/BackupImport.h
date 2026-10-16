/*
 File: BackupImport.h

 Newton Connection 1 backups (.nbku, PORTING.md 4.4), as the Cocoa app's
 BackupDocument read them: into a device file in memory, read only to the
 user (the UI may save it as a .newtondevice). The format:
   char[8]    "backup0"
   uint32 x3  the SourceInfo: version, manufacturer, machine type; in the
              byte order of the Mac that wrote it
   NSOF       the store frame: its apps (with their soups' names) and soups
   for each app, for each of its soups:
     NSOF     the soup's indexes
     NSOF     the soup's info
     NSOF...  the soup's entries, then NIL
 Soups in the store frame but in no app have no data: they become orphan
 soups. In the Packages app only entries with a pkgRef count.
 For the dock thread (Newton objects).
 */

#ifndef NCX_MODEL_BACKUPIMPORT_H
#define NCX_MODEL_BACKUPIMPORT_H

#include "model/DeviceFile.h"

#include <filesystem>
#include <memory>
#include <string>

namespace ncx {

/** The backup in a file. The device is named after the file. nullptr and
    why if it can't be read. */
std::unique_ptr<DeviceFile> ImportBackup(const std::filesystem::path & inPath, std::string & outError);

/** The backup in memory, the device named inName. */
std::unique_ptr<DeviceFile> ImportBackup(const Bytes & inData, const std::string & inName, std::string & outError);

/** True if the bytes start like a backup. */
bool IsBackup(const Bytes & inData);

} // namespace ncx

#endif // NCX_MODEL_BACKUPIMPORT_H
