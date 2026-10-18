/*
 File: PassthroughArea.h

 Where the keyboard panel takes what is typed (PORTING.md 7.4; the Cocoa
 app's NCXPassthruView): it takes the focus when clicked, and hands keys
 (as Newton characters), text, pasted text, and dropped text to its
 handlers. It shows that it has the focus.
 */

#ifndef NCX_UI_PANELS_PASSTHROUGHAREA_H
#define NCX_UI_PANELS_PASSTHROUGHAREA_H

#include <FL/Fl_Box.H>

#include <functional>
#include <string>

namespace ncx {

class PassthroughArea : public Fl_Box
{
public:
  PassthroughArea(int inX, int inY, int inW, int inH, const char * inLabel = nullptr);

  /** Takes keys and text while on. */
  void setOn(bool inOn);
  bool on() const { return mOn; }
  void setText(std::function<void(const std::string &)> inText) { mText = std::move(inText); }
  void setKey(std::function<void(char16_t)> inKey) { mKey = std::move(inKey); }
  /** Ask for the clipboard's text (Edit > Paste): it comes as FL_PASTE. */
  void paste();

  int handle(int inEvent) override;
  void draw() override;

private:
  bool mOn = false;
  bool mDropping = false;
  std::function<void(const std::string &)> mText;
  std::function<void(char16_t)> mKey;
};

/** The Newton character for a key that isn't text (FLTK's key code), 0 for
    others: BackSpace (the Mac's Delete) 0x08, Tab, Return, the arrows, F1 ...
    F15 (NCXPassthruView's map). */
char16_t NewtonKeyFor(int inFLTKKey);

} // namespace ncx

#endif // NCX_UI_PANELS_PASSTHROUGHAREA_H
