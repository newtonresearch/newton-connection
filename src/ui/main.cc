/*
 File: main.cc

 NCX, the app. For now (PORTING.md 1.1) a window with a menu bar that says
 whether the core works; it prints the core's self test when it starts.

   NCX                         the window
   NCX --selftest              the self test only, no window (exit 0 if all
                               is well)
   NCX --screenshot <file>     the window, drawn into a PNG file when it is
                               shown (its content: no frame, and on macOS no
                               menu bar), then quit; for checking the UI

 The menu bar: Fl_Sys_Menu_Bar, at the top of the screen on macOS and in the
 window elsewhere (D9 is open: one window or several).
 */

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Sys_Menu_Bar.H>
#include <FL/fl_ask.H>

#include "base/Core.h"
#include "base/Threads.h"
#include "base/Version.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

namespace {

const int kMenuHeight = 25;

void QuitCB(Fl_Widget *, void *)
{
  while (Fl_Window * window = Fl::first_window())
    window->hide();
}

/** --screenshot: the window into a PNG, then quit. */
struct Screenshot
{
  Fl_Window * window;
  std::string file;
  bool written = false;
};

void ScreenshotCB(void * inData)
{
  Screenshot * shot = static_cast<Screenshot *>(inData);
  Fl_Image_Surface surface(shot->window->w(), shot->window->h());
  Fl_Surface_Device::push_current(&surface);
  surface.draw(shot->window);
  Fl_RGB_Image * image = surface.image();
  Fl_Surface_Device::pop_current();
  shot->written = image && fl_write_png(shot->file.c_str(), image) == 0;
  delete image;
  QuitCB(nullptr, nullptr);
}

/** The dock thread's way to the UI thread (RunOnMainThread). */
void RunOnFLTKThread(std::function<void()> inTask)
{
  auto * task = new std::function<void()>(std::move(inTask));
  Fl::awake([](void * inData) {
    auto * task = static_cast<std::function<void()> *>(inData);
    (*task)();
    delete task;
  }, task);
}

void AboutCB(Fl_Widget *, void *)
{
  fl_message_title("About " NCX_NAME);
  fl_message("%s %s\n\nThe Newton's desktop companion,\nfor Windows, Linux and macOS.", NCX_NAME, NCX_VERSION);
}

} // namespace


int main(int argc, char * argv[])
{
  std::string report;
  bool ok = ncx::SelfTest(report);
  std::fputs(report.c_str(), stdout);
  if (argc == 2 && std::strcmp(argv[1], "--selftest") == 0)
    return ok ? 0 : 1;
  Screenshot shot;
  if (argc == 3 && std::strcmp(argv[1], "--screenshot") == 0) {
    shot.file = argv[2];
    argc = 1;     // not for FLTK
  }

  Fl::lock();                     // other threads may call Fl::awake
  ncx::SetMainThreadRunner(RunOnFLTKThread);
  ncx::StartLogging();

  Fl_Double_Window window(480, 320, NCX_NAME);
  Fl_Sys_Menu_Bar menu(0, 0, window.w(), kMenuHeight);
  menu.add("&File/&Quit", FL_COMMAND + 'q', QuitCB);
  menu.add("&Help/&About " NCX_NAME, 0, AboutCB);
  // on macOS the menu bar isn't in the window: the content starts at the top
  int top = Fl_Sys_Menu_Bar::window_menu_style() == Fl_Sys_Menu_Bar::no_window_menu ? 0 : kMenuHeight;
  std::string text = std::string(NCX_NAME " ") + ncx::Version() + "\n\n"
                   + (ok ? "The core works." : "The core doesn't work:\n\n" + report);
  Fl_Box box(0, top, window.w(), window.h() - top);
  box.copy_label(text.c_str());
  window.resizable(box);
  window.end();
  window.show(argc, argv);
  if (!shot.file.empty()) {
    // at once, not on a timer: the image surface draws off the screen, and
    // a timer may wait long while the window can't be seen (App Nap, a
    // locked screen)
    Fl::check();
    shot.window = &window;
    ScreenshotCB(&shot);
  }
  int result = shot.file.empty() ? Fl::run() : 0;
  if (!shot.file.empty() && !shot.written) {
    std::fprintf(stderr, "NCX: couldn't write %s\n", shot.file.c_str());
    return 1;
  }
  return result;
}
