/*
 File: Help.cc

 The help (Help.h).
 */

#include "ui/Help.h"
#include "ui/Images.h"

#include "base/Version.h"

#include <FL/Fl.H>
#include <FL/Fl_Help_View.H>
#include <FL/filename.H>

#include <cstring>
#include <string>

namespace ncx {

namespace {

/** Links: within the help, followed; to the web, opened in the browser. */
const char * LinkCB(Fl_Widget *, const char * inURI)
{
  if (std::strncmp(inURI, "http:", 5) == 0 || std::strncmp(inURI, "https:", 6) == 0
      || std::strncmp(inURI, "mailto:", 7) == 0) {
    fl_open_uri(inURI);
    return nullptr;
  }
  return inURI;
}

} // namespace


HelpWindow & HelpWindow::Show(const char * inSection)
{
  static HelpWindow * window = nullptr;
  if (!window) {
    Fl_Group::current(nullptr);
    window = new HelpWindow();
  }
  window->show();
  if (inSection && *inSection)
    window->mView->topline(inSection);
  else
    window->mView->topline(0);
  return *window;
}


HelpWindow::HelpWindow()
  : Fl_Double_Window(560, 640, NCX_NAME " Help")
{
  mView = new Fl_Help_View(0, 0, w(), h());
  mView->textfont(FL_HELVETICA);
  mView->textsize(14);
  mView->link(LinkCB);
  mView->value(BuiltinHelp("help"));
  resizable(mView);
  size_range(300, 200);
  end();
}

} // namespace ncx
