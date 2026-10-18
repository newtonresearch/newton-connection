/*
 File: PassthroughArea.cc

 Where the keyboard panel takes what is typed (PassthroughArea.h).
 */

#include "ui/panels/PassthroughArea.h"

#include "dock/Keyboard.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

namespace ncx {

char16_t NewtonKeyFor(int inFLTKKey)
{
  switch (inFLTKKey) {
    case FL_BackSpace:  return newtonkey::kBackspace;
    case FL_Tab:        return newtonkey::kTab;
    case FL_Enter:
    case FL_KP_Enter:   return newtonkey::kReturn;
    case FL_Left:       return newtonkey::kLeft;
    case FL_Right:      return newtonkey::kRight;
    case FL_Up:         return newtonkey::kUp;
    case FL_Down:       return newtonkey::kDown;
  }
  if (inFLTKKey >= FL_F + 1 && inFLTKKey <= FL_F + 15)
    return (char16_t)(newtonkey::kF1 + (inFLTKKey - FL_F - 1));
  return 0;
}


PassthroughArea::PassthroughArea(int inX, int inY, int inW, int inH, const char * inLabel)
  : Fl_Box(inX, inY, inW, inH, inLabel)
{
}


void PassthroughArea::setOn(bool inOn)
{
  mOn = inOn;
  redraw();
}


void PassthroughArea::paste()
{
  if (mOn)
    Fl::paste(*this, 1);
}


int PassthroughArea::handle(int inEvent)
{
  switch (inEvent) {
    case FL_PUSH:
      if (mOn && Fl::focus() != this)
        take_focus();
      return 1;
    case FL_FOCUS:
    case FL_UNFOCUS:
      redraw();
      return mOn ? 1 : 0;
    case FL_KEYBOARD: {
      if (!mOn || Fl::focus() != this)
        return 0;
      // shortcuts (the command key; Ctrl off macOS) are the menus'
      if (Fl::event_state(FL_COMMAND))
        return 0;
      char16_t key = NewtonKeyFor(Fl::event_key());
      if (key) {
        if (mKey)
          mKey(key);
        return 1;
      }
      if (Fl::event_length() > 0) {
        if (mText)
          mText(std::string(Fl::event_text(), Fl::event_length()));
        return 1;
      }
      return 0;
    }
    case FL_DND_ENTER:
    case FL_DND_DRAG:
      mDropping = mOn;
      return mDropping ? 1 : 0;
    case FL_DND_LEAVE:
      mDropping = false;
      return 1;
    case FL_DND_RELEASE:
      return mDropping ? 1 : 0;
    case FL_PASTE:
      mDropping = false;
      if (mOn && Fl::event_length() > 0 && mText)
        mText(std::string(Fl::event_text(), Fl::event_length()));
      return 1;
  }
  return Fl_Box::handle(inEvent);
}


void PassthroughArea::draw()
{
  Fl_Color background = mOn ? FL_BACKGROUND2_COLOR : fl_color_average(FL_BACKGROUND2_COLOR, FL_BACKGROUND_COLOR, 0.5f);
  draw_box(box(), background);
  if (mOn && Fl::focus() == this) {
    // the focus: a frame inside
    fl_color(FL_SELECTION_COLOR);
    fl_rect(x() + 3, y() + 3, w() - 6, h() - 6);
    fl_rect(x() + 4, y() + 4, w() - 8, h() - 8);
  }
  draw_label();
}

} // namespace ncx
