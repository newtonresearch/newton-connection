/*
 File: KeyboardPanel.cc

 The keyboard panel (KeyboardPanel.h).
 */

#include "ui/panels/KeyboardPanel.h"
#include "ui/App.h"
#include "ui/DocumentWindow.h"

#include <FL/Fl.H>
#include <FL/Fl_Button.H>

namespace ncx {

KeyboardPanel::KeyboardPanel(int inX, int inY, int inW, int inH)
  : KeyboardPanelUI(inX, inY, 560, 400)      // as designed, then to the size asked
{
  box(FL_NO_BOX);
  resize(inX, inY, inW, inH);
  mStart->callback(StartCB, this);
  mArea->setText([](const std::string & inText) { App::get().dock().typeOnKeyboard(inText); });
  mArea->setKey([](char16_t inKey) { App::get().dock().pressKey(inKey); });
  updateState();
}


void KeyboardPanel::setInstructions(const std::string & inText)
{
  mInstructions->copy_label(inText.c_str());
  redraw();
}


std::string KeyboardPanel::instructions() const
{
  return mInstructions->label() ? mInstructions->label() : "";
}


void KeyboardPanel::updateState()
{
  App & app = App::get();
  bool docked = app.isDockedWindow(dynamic_cast<DocumentWindow *>(window())) && !app.dock().newton1();
  if (docked && !mArea->on())
    mStart->activate();
  else
    mStart->deactivate();
}


void KeyboardPanel::enter()
{
  App & app = App::get();
  if (!app.isDockedWindow(dynamic_cast<DocumentWindow *>(window())) || app.dock().newton1()) {
    setInstructions("Not connected.");
    mActive = false;
    mArea->setOn(false);
    updateState();
    return;
  }
  if (app.dock().keyboardActive()) {
    // the Newton started it (Dock's Keyboard)
    mActive = true;
    changed(true);
    return;
  }
  if (!mActive) {
    mActive = true;
    setInstructions("Please wait...");
    app.dock().startKeyboard();
  }
  updateState();
}


void KeyboardPanel::leave()
{
  if (mActive && App::get().docked() && mArea->on())
    App::get().dock().stopKeyboard();
  mActive = false;
  mArea->setOn(false);
  setInstructions("Not connected.");
  updateState();
}


void KeyboardPanel::changed(bool inOn)
{
  mArea->setOn(inOn);
  if (inOn) {
    mActive = true;
    setInstructions("Set an insertion point on your Newton device and start typing.");
    if (visible_r())
      mArea->take_focus();
  } else {
    mActive = false;
    setInstructions(App::get().docked() ? "The keyboard is off: Start Typing turns it on again." : "Not connected.");
  }
  updateState();
}


bool KeyboardPanel::canPaste() const
{
  return visible_r() && mArea->on();
}


void KeyboardPanel::paste()
{
  if (canPaste())
    mArea->paste();
}


void KeyboardPanel::StartCB(Fl_Widget *, void * inPanel)
{
  KeyboardPanel * panel = static_cast<KeyboardPanel *>(inPanel);
  if (!App::get().docked())
    return;
  panel->mActive = true;
  panel->setInstructions("Please wait...");
  App::get().dock().startKeyboard();
  panel->updateState();
}

} // namespace ncx
