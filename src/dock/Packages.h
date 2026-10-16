/*
 File: Packages.h

 Installing packages on the Newton (PORTING.md 5.2.3; the Cocoa app's
 NCPackageComponent and the install queue of its NCDockProtocolController).
 The desktop takes control, asks to install, and for each package asks the
 'pfnd' extension (the Cocoa app's) whether one of its name is installed
 already: then it is replaced only if asked to (removed first), otherwise
 left as it is. A package goes to the default store. A NewtonOS 1 Newton
 just takes the packages, and the session ends after them.

 On the dock thread. Reading a package file's name needs no Newton.
 */

#ifndef NCX_DOCK_PACKAGES_H
#define NCX_DOCK_PACKAGES_H

#include "base/Progress.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ncx {

class DockController;

struct PackageFile
{
  std::string name;               // its unique name (UTF-8)
  uint64_t size = 0;              // of the file
};

/** A Newton package's name and size: false if the file isn't one (it
    starts with "package0" or "package1"). */
bool ReadPackageFile(const std::filesystem::path & inFile, PackageFile & outPackage);

/** A file NCX offers as a package: .newtonpkg, or a .pkg that is one (a
    Mac installer package is a .pkg too). */
bool IsNewtonPackage(const std::filesystem::path & inFile);

struct InstallResult
{
  std::filesystem::path file;
  std::string name;               // the package's
  int error = 0;                  // 0: installed (or left as it was: skipped)
  bool skipped = false;           // installed already, not replaced
};

class PackageInstaller
{
public:
  explicit PackageInstaller(DockController & inDock) : mDock(inDock) { }

  /** A package of that name is on the Newton already: replace it? (The UI
      asks; ncx-cli --replace.) Without one: it isn't replaced. */
  void setReplace(std::function<bool(const std::string &)> inReplace) { mReplace = std::move(inReplace); }

  /** Install the files in order. ioProgress: the bytes sent of all of
      them; cancel() stops before the next package (during one, the session
      ends). Disconnect on the Newton stops it too (kDockErrAborted). */
  std::vector<InstallResult> install(const std::vector<std::filesystem::path> & inFiles, Progress * ioProgress = nullptr);

  /** One package, no questions: the Newton asked for it ('lpfl', after
      browsing the desktop). 0 or the Newton's error. */
  int load(const std::filesystem::path & inFile, Progress * ioProgress = nullptr);

private:
  int send(const std::filesystem::path & inFile, Progress * ioProgress);

  DockController & mDock;
  std::function<bool(const std::string &)> mReplace;
};

} // namespace ncx

#endif // NCX_DOCK_PACKAGES_H
