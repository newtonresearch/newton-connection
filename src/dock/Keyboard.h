/*
 File: Keyboard.h

 Keyboard passthrough (PORTING.md 5.2.7; the Cocoa app's
 NCPassthruComponent, and the key mapping of its NCPassthruView): typing on
 the desktop goes to the Newton's insertion point.
   - The desktop starts it (in control, kDStartKeyboardPassthrough) and the
     Newton answers with the same and shows its keyboard slip; or the
     Newton starts it ('kybd' from it: Dock's Keyboard).
   - Text goes as kDKeyboardChar (one character and the command key) or
     kDKeyboardString (big-endian UniChars), neither answered. A return is
     a CR (0x0D); characters beyond 16 bits become '?' (NewtonOS has no
     more).
   - The desktop ends it (kDOperationDone), or the Newton does ('opca': its
     slip closed), which is acknowledged.
 While it's on, the session doesn't time out (the user may type nothing for
 a while; the tickler keeps the Newton awake).

 The Keyboard registers its handlers ('kybd', 'opca') with the controller,
 so make it before the controller starts and keep it while it runs.
 start(), type(), key() and stop() may be called on any thread (they post
 to the dock thread); begin(), send(), sendKey() and end() are the same on
 the dock thread, for ncx-cli and other operations.
 */

#ifndef NCX_DOCK_KEYBOARD_H
#define NCX_DOCK_KEYBOARD_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace ncx {

class DockController;

/** Newton characters for the keys that aren't text (NCPassthruView). */
namespace newtonkey {
inline constexpr char16_t kBackspace = 0x08;    // the desktop's Delete
inline constexpr char16_t kTab = 0x09;
inline constexpr char16_t kReturn = 0x0D;
inline constexpr char16_t kLeft = 0x1C;
inline constexpr char16_t kRight = 0x1D;
inline constexpr char16_t kUp = 0x1E;
inline constexpr char16_t kDown = 0x1F;
inline constexpr char16_t kF1 = 0xF721;         // F1 ... F15: kF1 + 0 ... 14
}

/** kDKeyboardChar's flags. The Newton seems to ignore them. */
inline constexpr uint16_t kKeyboardCommandKey = 1;

/** Text as the Newton takes it: UTF-16, CR for a line's end ("\r\n" and
    "\n" too), '?' for a character beyond 16 bits. */
std::u16string NewtonKeyboardText(const std::string & inUTF8);

class Keyboard
{
public:
  explicit Keyboard(DockController & ioDock);

  /** On (true) or off: the Newton answered or started it, or it ended (on
      the Newton, or stop()). On the dock thread. */
  void setChanged(std::function<void(bool)> inChanged) { mChanged = std::move(inChanged); }
  /** On, in this session. */
  bool active() const;

  /*-- any thread --*/

  void start();
  void type(const std::string & inUTF8);
  void key(char16_t inKey, uint16_t inFlags = 0);
  void stop();

  /*-- the dock thread --*/

  /** Ask the Newton; 0 once it answered, or why not. */
  int begin();
  /** One character goes as kDKeyboardChar, more as kDKeyboardString (in
      pieces of at most kStringPiece). */
  void send(const std::string & inUTF8);
  void sendKey(char16_t inKey, uint16_t inFlags = 0);
  void end();

  static constexpr size_t kStringPiece = 128;

private:
  void activate();
  void deactivate();

  DockController & mDock;
  std::function<void(bool)> mChanged;
  std::atomic<bool> mActive { false };
  std::atomic<unsigned> mSession { 0 };      // the session it's on in
};

} // namespace ncx

#endif // NCX_DOCK_KEYBOARD_H
