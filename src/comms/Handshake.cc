/*
 File: Handshake.cc

 The start of a dock session (Handshake.h).
 */

#include "comms/Handshake.h"
#include "base/Log.h"
#include "base/NewtonStrings.h"
#include "base/Strings.h"

#include "Communications/DES.h"
#include "RSSymbols.h"

#include <cstdio>
#include <random>

namespace ncx {

namespace {

void PutLong(std::vector<uint8_t> & ioBytes, uint32_t inValue)
{
  for (int shift = 24; shift >= 0; shift -= 8)
    ioBytes.push_back((uint8_t)(inValue >> shift));
}

} // namespace


void EncryptNonce(uint32_t & ioHi, uint32_t & ioLo, const std::string & inPassword)
{
  // the password as UniChars, 0-terminated, in the host's order (as the
  // Cocoa app and newtdump pass them)
  std::u16string chars = UTF16FromUTF8(inPassword);
  std::vector<UniChar> password(chars.begin(), chars.end());
  password.push_back(0);
  SNewtNonce key;
  DESCharToKey(password.data(), &key);
  SNewtNonce nonce = { ioHi, ioLo };
  DESEncodeNonce(&key, &nonce);
  ioHi = nonce.hi;
  ioLo = nonce.lo;
}


std::string MachineName(uint32_t inMachineType)
{
  // the Newton's gestalt machine types (newtdump knows these two)
  switch (inMachineType) {
    case 0x10003000: return "MessagePad 2000/2100";
    case 0x10004000: return "eMate 300";
  }
  char text[16];
  std::snprintf(text, sizeof(text), "0x%08X", inMachineType);
  return text;
}


DockedNewton Handshake(Session & ioSession, const std::string & inPassword, const DockAppInfo & inApp, int inWaitSeconds)
{
  DockedNewton newton;

  // request to dock: the Newton's protocol version (9: negotiated upwards later)
  DockEvent request = ioSession.receive(kDRequestToDock, inWaitSeconds);
  if (request.command != kDRequestToDock && request.command != kDRequestToAutoDock)
    throw DockFailure(kDockErrProtocolError, "'" + CmdName(request.command) + "' instead of a request to dock");
  newton.protocolVersion = (uint32_t)request.value();
  ioSession.sendValue(kDInitiateDocking, inApp.sessionType);

  // its name: the length of its info, its info, its owner's name (UniChars, big-endian)
  DockEvent name = ioSession.receive(kDNewtonName);
  if (name.command != kDNewtonName)
    throw DockFailure(kDockErrProtocolError, "'" + CmdName(name.command) + "' instead of the Newton's name");
  uint32_t infoLength = name.longAt(0);
  uint32_t * info = reinterpret_cast<uint32_t *>(&newton.info);
  for (size_t i = 0; i < sizeof(NewtonInfo) / 4 && 4 + i * 4 + 4 <= 4 + (size_t)infoLength; ++i)
    info[i] = name.longAt(4 + i * 4);
  std::u16string owner;
  for (size_t i = 4 + infoLength; i + 1 < name.data.size(); i += 2) {
    char16_t c = (char16_t)(name.data[i] << 8 | name.data[i + 1]);
    if (c == 0)
      break;
    owner += c;
  }
  newton.name = UTF8FromUTF16(owner);
  NCX_LOG_MINIMUM("The Newton: \"%s\", %s, ROM %u.%u", newton.name.c_str(), MachineName(newton.info.fMachineType).c_str(),
                  newton.info.fROMVersion >> 16, newton.info.fROMVersion & 0xFFFF);

  if (newton.info.fROMVersion < 0x00020000) {
    // NewtonOS 1 doesn't speak the Dante protocol: the old one's short version
    newton.newton1 = true;
    ioSession.sendValue(kDSetTimeout, 90);
    ioSession.receiveResult();
    return newton;
  }

  // the desktop's info: protocol, desktop type, our nonce, session type,
  // selective sync, and the app ([{name, id, version, doesAuto}])
  std::random_device random;
  uint32_t deskHi = random(), deskLo = random();
  std::vector<uint8_t> desktop;
  PutLong(desktop, kDanteProtocolVersion);
  PutLong(desktop, (uint32_t)inApp.desktopType);
  PutLong(desktop, deskHi);
  PutLong(desktop, deskLo);
  PutLong(desktop, (uint32_t)inApp.sessionType);
  PutLong(desktop, 1);                       // selective sync allowed
  RefVar app(AllocateFrame());
  SetFrameSlot(app, SYMA(name), MakeStringFromUTF8(inApp.name));
  SetFrameSlot(app, SYMA(id), MAKEINT(inApp.id));
  SetFrameSlot(app, SYMA(version), MAKEINT(inApp.version));
  SetFrameSlot(app, MakeSymbol("doesAuto"), TRUEREF);    // undocumented, but NCU does it
  RefVar apps(MakeArray(1));
  SetArraySlot(apps, 0, app);
  std::vector<uint8_t> appBytes = FlattenToBytes(apps);
  desktop.insert(desktop.end(), appBytes.begin(), appBytes.end());
  ioSession.send(DockEvent::MakeData(kDDesktopInfo, std::move(desktop)));

  // its info: the protocol version it settles on, its nonce
  DockEvent newtonInfo = ioSession.receive(kDNewtonInfo);
  newton.protocolVersion = newtonInfo.longAt(0);
  uint32_t newtHi = newtonInfo.longAt(4), newtLo = newtonInfo.longAt(8);

  ioSession.sendValue(kDWhichIcons, inApp.icons);
  ioSession.receiveResult();
  ioSession.sendValue(kDSetTimeout, 90);    // the Newton answers with its password

  // the password: our nonce encrypted with the Newton's password must be
  // ours encrypted with ours; then the Newton's nonce encrypted with ours
  uint32_t expectedHi = deskHi, expectedLo = deskLo;
  EncryptNonce(expectedHi, expectedLo, inPassword);
  for (int retries = inApp.passwordRetries; ; --retries) {
    DockEvent password = ioSession.receive(kDPassword);
    if (password.command == kDPassword && password.longAt(0) == expectedHi && password.longAt(4) == expectedLo)
      break;
    if (retries <= 0) {
      ioSession.sendValue(kDResult, kDockErrBadPasswordError);
      throw DockFailure(kDockErrBadPasswordError, "the password doesn't match");
    }
    ioSession.send(kDPWWrong);
  }
  EncryptNonce(newtHi, newtLo, inPassword);
  std::vector<uint8_t> answer;
  PutLong(answer, newtHi);
  PutLong(answer, newtLo);
  ioSession.send(DockEvent::MakeData(kDPassword, std::move(answer)));
  return newton;
}

} // namespace ncx
