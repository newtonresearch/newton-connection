/*
 File: Keyboard.cc

 Keyboard passthrough (Keyboard.h).
 */

#include "dock/Keyboard.h"
#include "dock/DockController.h"
#include "base/Log.h"
#include "base/Strings.h"

#include <vector>

namespace ncx {

namespace {

void PutChar(std::vector<uint8_t> & ioBytes, char16_t inChar)
{
  ioBytes.push_back((uint8_t)(inChar >> 8));
  ioBytes.push_back((uint8_t)inChar);
}

} // namespace


std::u16string NewtonKeyboardText(const std::string & inUTF8)
{
  std::u16string text = UTF16FromUTF8(inUTF8);
  std::u16string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    char16_t ch = text[i];
    if (ch == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n')
      ++i;
    if (ch == u'\n')
      ch = u'\r';
    if (ch >= 0xD800 && ch < 0xDC00 && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000) {
      ++i;                          // a pair: one character, beyond 16 bits
      ch = u'?';
    } else if (ch >= 0xD800 && ch < 0xE000) {
      ch = u'?';
    }
    out.push_back(ch);
  }
  return out;
}


Keyboard::Keyboard(DockController & ioDock) : mDock(ioDock)
{
  // the Newton starts it (Dock's Keyboard)
  mDock.handle(kDStartKeyboardPassthrough, [this](DockController &, const DockEvent &) {
    Log(kLogMinimum, "keyboard: the Newton started it");
    activate();
  });
  // the Newton ends it (or cancels with nothing going on): acknowledged
  mDock.handle(kDOperationCanceled, [this](DockController & ioDock, const DockEvent &) {
    ioDock.session().send(kDOpCanceledAck);
    ioDock.session().suppressTimeout(false);
    if (active())
      Log(kLogMinimum, "keyboard: the Newton ended it");
    deactivate();
  });
}


bool Keyboard::active() const
{
  return mActive && mDock.isDocked() && mSession == mDock.sessionNumber();
}


void Keyboard::activate()
{
  mSession = mDock.sessionNumber();
  mActive = true;
  mDock.session().suppressTimeout(true);
  if (mChanged)
    mChanged(true);
}


void Keyboard::deactivate()
{
  bool was = mActive.exchange(false);
  if (was && mChanged)
    mChanged(false);
}


void Keyboard::start()
{
  mDock.post([this](DockController &) { begin(); });
}


void Keyboard::type(const std::string & inUTF8)
{
  mDock.post([this, inUTF8](DockController &) { send(inUTF8); });
}


void Keyboard::key(char16_t inKey, uint16_t inFlags)
{
  mDock.post([this, inKey, inFlags](DockController &) { sendKey(inKey, inFlags); });
}


void Keyboard::stop()
{
  mDock.post([this](DockController &) { end(); });
}


int Keyboard::begin()
{
  if (active())
    return 0;
  Session & session = mDock.session();
  mDock.setDesktopControl(true);
  session.send(kDStartKeyboardPassthrough);
  DockEvent answer = session.receive(kDStartKeyboardPassthrough);
  if (answer.command != kDStartKeyboardPassthrough) {
    // a Newton without it ('unkn')
    mDock.setDesktopControl(false);
    Log(kLogMinimum, "keyboard: the Newton answered '%s'", CmdName(answer.command).c_str());
    return kDockErrProtocolError;
  }
  activate();
  return 0;
}


void Keyboard::send(const std::string & inUTF8)
{
  if (!active())
    return;
  std::u16string text = NewtonKeyboardText(inUTF8);
  if (text.size() == 1) {
    sendKey(text[0]);
    return;
  }
  for (size_t at = 0; at < text.size(); at += kStringPiece) {
    std::vector<uint8_t> data;
    for (size_t i = at; i < text.size() && i < at + kStringPiece; ++i)
      PutChar(data, text[i]);
    PutChar(data, 0);
    mDock.session().send(DockEvent::MakeData(kDKeyboardString, std::move(data)));
  }
}


void Keyboard::sendKey(char16_t inKey, uint16_t inFlags)
{
  if (!active())
    return;
  std::vector<uint8_t> data;
  PutChar(data, inKey);
  PutChar(data, (char16_t)inFlags);
  mDock.session().send(DockEvent::MakeData(kDKeyboardChar, std::move(data)));
}


void Keyboard::end()
{
  if (!active())
    return;
  mDock.setDesktopControl(false);       // kDOperationDone; timeouts again
  deactivate();
}

} // namespace ncx
