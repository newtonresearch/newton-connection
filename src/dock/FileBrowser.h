/*
 File: FileBrowser.h

 The Newton browsing the desktop's folders (PORTING.md 5.1; the Cocoa app's
 do_rtbr, do_dpth, do_spth, do_gfil, do_gfin, buildPath, buildFileList,
 buildFileInfo): Dock's Install (and Import, Restore) on the Newton asks
 for a folder, its files, a file's info, and then for a package to install
 ('lpfl'), which it installs (Packages.h). The Newton sees:
   the path   [computer, disk, folder, ...]: {name, type} each (kDesktop,
              kDesktopDisk "/" or "C:\", kDesktopFolder)
   the files  the folders, and the files of the kind it browses for:
              'packages (Packages.h IsNewtonPackage), 'syncFiles (backups:
              .newtondevice, .nbku), 'import (what importable() says)
 Hidden files (a dot first) are left out. On the dock thread; it registers
 its handlers with the controller, before start().
 */

#ifndef NCX_DOCK_FILEBROWSER_H
#define NCX_DOCK_FILEBROWSER_H

#include "comms/DockEvent.h"

#include "Objects.h"

#include <filesystem>
#include <functional>
#include <string>

namespace ncx {

class DockController;

class FileBrowser
{
public:
  /** Registers its handlers with the controller. */
  explicit FileBrowser(DockController & ioDock);

  /** The folder the Newton starts in (the user's home folder). */
  void setFolder(const std::filesystem::path & inFolder) { mFolder = inFolder; }
  const std::filesystem::path & folder() const { return mFolder; }

  /** Files the Newton may import ('import): none until the translators
      come (Phase 6). */
  void setImportable(std::function<bool(const std::filesystem::path &)> inImportable) { mImportable = std::move(inImportable); }
  /** A package the Newton installed from the desktop: its file, 0 or the
      error. On the dock thread. */
  void setInstalled(std::function<void(const std::filesystem::path &, int)> inInstalled) { mInstalled = std::move(inInstalled); }

private:
  enum Kind { kNothing, kPackages, kSyncFiles, kImport };

  void requestToBrowse(DockController & ioDock, const DockEvent & inEvent);
  void setPath(DockController & ioDock, const DockEvent & inEvent);
  void fileInfo(DockController & ioDock, const DockEvent & inEvent);
  void loadPackageFile(DockController & ioDock, const DockEvent & inEvent);
  bool shows(const std::filesystem::path & inFile) const;
  Ref path() const;
  Ref fileList() const;
  std::filesystem::path file(const DockEvent & inEvent) const;

  std::filesystem::path mFolder;
  Kind mKind = kNothing;
  std::function<bool(const std::filesystem::path &)> mImportable;
  std::function<void(const std::filesystem::path &, int)> mInstalled;
};

} // namespace ncx

#endif // NCX_DOCK_FILEBROWSER_H
