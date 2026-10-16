/*
 File: Handshake.h

 The start of a dock session, the desktop's side (the Cocoa app's do_rtdk,
 do_name, do_ninf, do_pass): request to dock, the Newton's name and info,
 the desktop's info, the icons, the password both ways. A NewtonOS 1
 Newton gets the old protocol's short version.
 */

#ifndef NCX_COMMS_HANDSHAKE_H
#define NCX_COMMS_HANDSHAKE_H

#include "comms/DockProtocol.h"
#include "comms/Session.h"

#include <cstdint>
#include <string>

namespace ncx {

/** What the desktop says it is (the Cocoa app's gNCAppInfo). */
struct DockAppInfo
{
  std::string name = "Newton Connection";
  int id = 2;
  int version = 1;
  int sessionType = kSettingUpSession;
  int icons = kBackupIcon | kInstallIcon | kImportIcon | kKeyboardIcon;
  int passwordRetries = 2;
  /** NCU on Windows said so; the Newton's file browser shows Windows
      paths then. */
#if defined(_WIN32)
  int desktopType = kWindowsDesktop;
#else
  int desktopType = kMacDesktop;
#endif
};

/** The Newton, as the handshake found it. */
struct DockedNewton
{
  uint32_t protocolVersion = 0;   // 9 (NewtonOS 1) or 10 (Dante)
  NewtonInfo info;
  std::string name;               // its owner's name (UTF-8)
  bool newton1 = false;           // NewtonOS 1: the old protocol
};

/** The handshake, until the password is through. inWaitSeconds: how long
    to wait for the Newton's request (-1: no limit). Throws DockFailure
    (the password doesn't match after the retries: kDockErrBadPasswordError). */
DockedNewton Handshake(Session & ioSession, const std::string & inPassword, const DockAppInfo & inApp = DockAppInfo(),
                       int inWaitSeconds = -1);

/** A 64-bit nonce encrypted with a password (UTF-8; "" none), as the
    Newton does it (DES, Communications/DES.c). */
void EncryptNonce(uint32_t & ioHi, uint32_t & ioLo, const std::string & inPassword);

/** "MessagePad 2000/2100", "eMate 300"; or the number in hex. */
std::string MachineName(uint32_t inMachineType);

} // namespace ncx

#endif // NCX_COMMS_HANDSHAKE_H
