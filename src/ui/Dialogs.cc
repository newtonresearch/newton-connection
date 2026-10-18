/*
 File: Dialogs.cc

 The app's small dialogs (Dialogs.h).
 */

#include "ui/Dialogs.h"

#include "base/Prefs.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Return_Button.H>

namespace ncx {

namespace {

void CloseCB(Fl_Widget * inButton, void * inAnswer)
{
  *static_cast<int *>(inAnswer) = inButton->argument();
  inButton->window()->hide();
}

} // namespace


bool ConfirmUnlessSilenced(const char * inPrefKey, const std::string & inTitle, const std::string & inMessage,
                           const std::string & inDetail, const std::string & inGoLabel)
{
  if (Prefs::App().getBool(inPrefKey))
    return true;
  int answer = 0;
  Fl_Group::current(nullptr);
  Fl_Double_Window window(440, 170, inTitle.c_str());
  Fl_Box icon(16, 16, 48, 48, "!");
  icon.box(FL_OVAL_BOX);
  icon.color(FL_YELLOW);
  icon.labelfont(FL_HELVETICA_BOLD);
  icon.labelsize(28);
  Fl_Box message(76, 14, 348, 28, inMessage.c_str());
  message.labelfont(FL_HELVETICA_BOLD);
  message.align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT | FL_ALIGN_WRAP);
  Fl_Box detail(76, 44, 348, 50, inDetail.c_str());
  detail.labelsize(13);
  detail.align(FL_ALIGN_INSIDE | FL_ALIGN_TOP_LEFT | FL_ALIGN_WRAP);
  Fl_Check_Button silence(76, 96, 348, 22, "Don't show this again");
  silence.labelsize(13);
  Fl_Button cancel(220, 130, 96, 28, "Cancel");
  cancel.callback(CloseCB, &answer);
  cancel.argument(0);
  cancel.shortcut(FL_Escape);
  Fl_Return_Button go(328, 130, 96, 28, inGoLabel.c_str());
  go.callback(CloseCB, &answer);
  go.argument(1);
  window.end();
  window.set_modal();
  window.show();
  while (window.shown())
    Fl::wait();
  if (answer == 1 && silence.value())
    Prefs::App().setBool(inPrefKey, true);
  return answer == 1;
}

} // namespace ncx
