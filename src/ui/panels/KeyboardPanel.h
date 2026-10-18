/*
 File: KeyboardPanel.h

 The keyboard panel (PORTING.md 7.4; the Cocoa app's
 NCKeyboardViewController): while it shows and the Newton is docked,
 keyboard passthrough is on (AppDock's keyboard), and what is typed,
 pasted or dropped on its area goes to the Newton's insertion point. The
 Newton may end it (its slip closed): Start Typing starts it again. Its
 layout: KeyboardPanelUI.fl.
 */

#ifndef NCX_UI_PANELS_KEYBOARDPANEL_H
#define NCX_UI_PANELS_KEYBOARDPANEL_H

#include "KeyboardPanelUI.h"

#include <string>

namespace ncx {

class KeyboardPanel : public KeyboardPanelUI
{
public:
  KeyboardPanel(int inX, int inY, int inW, int inH);

  /** Shown: passthrough starts while docked. */
  void enter();
  /** Hidden (another panel, undocked, the window closes): it ends. */
  void leave();
  /** From the dock (on the UI thread): on or off. */
  void changed(bool inOn);
  /** Edit > Paste: the clipboard's text goes to the Newton. */
  bool canPaste() const;
  void paste();

  bool active() const { return mActive; }
  bool on() const { return mArea->on(); }
  std::string instructions() const;

private:
  void setInstructions(const std::string & inText);
  void updateState();
  static void StartCB(Fl_Widget *, void *);

  bool mActive = false;     // passthrough asked for
};

} // namespace ncx

#endif // NCX_UI_PANELS_KEYBOARDPANEL_H
