/*
 File: Help.h

 The help (PORTING.md 7.9): resources/help/help.html, built into the app,
 in a window of its own (an Fl_Help_View: links within it followed, links
 to the web opened in the browser). One for the app.
 */

#ifndef NCX_UI_HELP_H
#define NCX_UI_HELP_H

#include <FL/Fl_Double_Window.H>

class Fl_Help_View;

namespace ncx {

class HelpWindow : public Fl_Double_Window
{
public:
  /** The window, shown at the top, or at a section ("backup", ...). */
  static HelpWindow & Show(const char * inSection = nullptr);

  Fl_Help_View * view() { return mView; }

private:
  HelpWindow();
  Fl_Help_View * mView = nullptr;
};

} // namespace ncx

#endif // NCX_UI_HELP_H
