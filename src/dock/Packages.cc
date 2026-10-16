/*
 File: Packages.cc

 Installing packages on the Newton (Packages.h).
 */

#include "dock/Packages.h"
#include "dock/DockController.h"
#include "base/Log.h"
#include "base/NewtonStrings.h"
#include "base/Strings.h"
#include "comms/DockRefs.h"
#include "platform/AppFolders.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace ncx {

namespace {

const EventType kPackageFinder = Cmd("pfnd");
const EventType kPackageNotThere = Cmd("pkno");
const EventType kPackageThere = Cmd("pkya");
const EventType kPackageInUse = Cmd("tran");

// a package's directory: the signature (8), ..., the name's InfoRef at 28,
// the number of parts at 52; the part entries (32 bytes each) after the 56
// bytes of the header; the name (UTF-16, big-endian) in the data after them
const size_t kHeaderSize = 56;
const size_t kPartEntrySize = 32;

uint32_t Long(const uint8_t * inBytes)
{
  return (uint32_t)inBytes[0] << 24 | (uint32_t)inBytes[1] << 16 | (uint32_t)inBytes[2] << 8 | inBytes[3];
}

} // namespace


bool ReadPackageFile(const std::filesystem::path & inFile, PackageFile & outPackage)
{
  std::ifstream in(inFile, std::ios::binary);
  uint8_t header[kHeaderSize];
  if (!in.read((char *)header, kHeaderSize) || std::memcmp(header, "package", 7) != 0
      || (header[7] != '0' && header[7] != '1'))
    return false;
  uint32_t numParts = Long(header + 52);
  uint32_t nameOffset = (uint32_t)header[28] << 8 | header[29], nameLength = (uint32_t)header[30] << 8 | header[31];
  if (numParts > 1000 || nameLength > 1024)
    return false;
  std::vector<uint8_t> name(nameLength);
  in.seekg((std::streamoff)(kHeaderSize + numParts * kPartEntrySize + nameOffset));
  if (nameLength && !in.read((char *)name.data(), nameLength))
    return false;
  std::u16string text;
  for (size_t i = 0; i + 1 < name.size(); i += 2) {
    char16_t c = (char16_t)(name[i] << 8 | name[i + 1]);
    if (c == 0)
      break;
    text.push_back(c);
  }
  in.clear();
  in.seekg(0, std::ios::end);
  outPackage.name = UTF8FromUTF16(text);
  outPackage.size = (uint64_t)in.tellg();
  return true;
}


bool IsNewtonPackage(const std::filesystem::path & inFile)
{
  std::string extension = UTF8FromPath(inFile.extension());
  std::transform(extension.begin(), extension.end(), extension.begin(), [](char c) { return (char)std::tolower((unsigned char)c); });
  if (extension != ".newtonpkg" && extension != ".pkg")
    return false;
  std::ifstream in(inFile, std::ios::binary);
  char signature[7];
  return in.read(signature, 7) && std::memcmp(signature, "package", 7) == 0;
}


int PackageInstaller::send(const std::filesystem::path & inFile, Progress * ioProgress)
{
  Session & session = mDock.session();
  if (!mDock.newton().newton1)
    session.setCurrentStore(RefVar(NILREF), false);      // the default store
  session.sendPackage(inFile, ioProgress);
  for (;;) {
    DockEvent answer = session.receive(kDAnyEvent);
    if (answer.command == kDResult)
      return answer.value();
    if (answer.command == kDOperationCanceled) {
      session.send(kDOpCanceledAck);
      return kDockErrAborted;
    }
    Log(kLogMinimum, "packages: '%s' instead of the result, ignored", CmdName(answer.command).c_str());
  }
}


int PackageInstaller::load(const std::filesystem::path & inFile, Progress * ioProgress)
{
  PackageFile package;
  if (!ReadPackageFile(inFile, package))
    return kDockErrPackageCantLoad;
  if (ioProgress)
    ioProgress->setDescription("Installing " + package.name);
  return send(inFile, ioProgress);
}


std::vector<InstallResult> PackageInstaller::install(const std::vector<std::filesystem::path> & inFiles,
                                                     Progress * ioProgress)
{
  std::vector<InstallResult> results;
  Session & session = mDock.session();
  bool newton2 = !mDock.newton().newton1;

  // the total, for the progress
  int64_t total = 0, before = 0;
  for (const std::filesystem::path & file : inFiles) {
    PackageFile package;
    if (ReadPackageFile(file, package))
      total += (int64_t)package.size;
  }
  if (ioProgress) {
    ioProgress->setTotal(total);
    ioProgress->setCompleted(0);
  }

  if (newton2) {
    mDock.setDesktopControl(true);
    mDock.loadExtension(kPackageFinder);
    session.send(kDRequestToInstall);
    session.receiveResult();
  }
  bool aborted = false;
  for (const std::filesystem::path & file : inFiles) {
    InstallResult result;
    result.file = file;
    PackageFile package;
    if (aborted || (ioProgress && ioProgress->cancelled())) {
      result.error = kDockErrAborted;
      results.push_back(result);
      continue;
    }
    if (!ReadPackageFile(file, package)) {
      result.error = kDockErrPackageCantLoad;
      results.push_back(result);
      continue;
    }
    result.name = package.name;
    if (ioProgress)
      ioProgress->setDescription("Installing " + package.name);

    bool install = true;
    if (newton2) {
      // installed already? (by name, on the default store)
      RefVar request(AllocateFrame());
      SetFrameSlot(request, MakeSymbol("packageName"), MakeStringFromUTF8(package.name));
      DockEvent answer = session.callExtension(kPackageFinder, request);
      if (answer.command == kDOperationCanceled) {
        session.send(kDOpCanceledAck);
        result.error = kDockErrAborted;
        aborted = true;
        install = false;
      } else if (answer.command == kDResult && answer.value() != 0) {
        result.error = answer.value();
        install = false;
      } else if (answer.command == kPackageThere || answer.command == kPackageInUse) {
        if (mReplace && mReplace(package.name)) {
          session.setCurrentStore(RefVar(NILREF), false);
          session.sendRef(kDRemovePackage, RefVar(MakeStringFromUTF8(package.name)));
          result.error = session.receiveResult();
          install = result.error == 0;
        } else {
          result.skipped = true;
          install = false;
        }
      }
    }
    if (install) {
      // this package's progress is a part of all of them
      Progress progress((int64_t)package.size);
      progress.setParent(ioProgress, before);
      result.error = send(file, ioProgress ? &progress : nullptr);
      if (result.error == kDockErrAborted)
        aborted = true;
    }
    before += (int64_t)package.size;
    if (ioProgress)
      ioProgress->setCompleted(before);
    results.push_back(result);
  }

  if (newton2) {
    // cancelled on the Newton: it no longer waits for the end of control
    if (!aborted || (ioProgress && ioProgress->cancelled()))
      mDock.setDesktopControl(false);
  } else {
    // NewtonOS 1: nothing else to do in this session
    mDock.disconnect();
  }
  return results;
}

} // namespace ncx
