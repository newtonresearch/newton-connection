/*
 File: main.cc

 NCX, the app (PORTING.md Phase 7): document windows, one per
 .newtondevice file (App.h), and a dock that waits for Newtons.

   NCX [<file.newtondevice> ...]   the app, with these backups open
   NCX --target <target> ...       listen there (more than once for several:
                                   tcp-all:<port>, tcp:<port>,
                                   serial:<device>[@<bps>], einstein-pipes),
                                   not where the preferences say
   NCX --selftest                  the core's self test only, no window
                                   (exit 0 if all is well)
   NCX --screenshot <png> [<file.newtondevice>]
                                   the first window (with that backup),
                                   drawn into a PNG file when it is shown
                                   (its content: no frame, and on macOS no
                                   menu bar), then quit; no dock. For
                                   checking the UI
 */

#include <FL/Fl.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/fl_ask.H>

#include "base/Core.h"
#include "base/Threads.h"
#include "platform/AppFolders.h"
#include "ui/App.h"
#include "ui/DocumentWindow.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

namespace {

/** --screenshot: a window into a PNG. */
bool WriteScreenshot(Fl_Window * inWindow, const std::string & inFile)
{
  Fl_Image_Surface surface(inWindow->w(), inWindow->h());
  Fl_Surface_Device::push_current(&surface);
  surface.draw(inWindow);
  Fl_RGB_Image * image = surface.image();
  Fl_Surface_Device::pop_current();
  bool written = image && fl_write_png(inFile.c_str(), image) == 0;
  delete image;
  return written;
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

} // namespace


int main(int argc, char * argv[])
{
  std::string report;
  bool ok = ncx::SelfTest(report);
  std::fputs(report.c_str(), stdout);
  if (argc == 2 && std::strcmp(argv[1], "--selftest") == 0)
    return ok ? 0 : 1;
  if (!ok) {
    fl_alert("Newton Connection can't start: its core doesn't work.\n\n%s", report.c_str());
    return 1;
  }

  ncx::App::Options options;
  std::string screenshot;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
      screenshot = argv[++i];
      options.dock = false;
    } else if (std::strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
      options.targets.push_back(argv[++i]);
    } else if (argv[i][0] != '-')
      options.files.push_back(ncx::PathFromUTF8(argv[i]));
    // macOS passes -psn_... to apps the Finder starts: not ours
  }

  Fl::lock();                     // other threads call Fl::awake, and draw (Canvas)
  ncx::SetMainThreadRunner(RunOnFLTKThread);
  ncx::StartLogging();
  ncx::App & app = ncx::App::get();
  app.start(options);

  int result = 0;
  if (!screenshot.empty()) {
    // at once, not on a timer: the image surface draws off the screen, and
    // a timer may wait long while the window can't be seen (App Nap, a
    // locked screen)
    Fl::check();
    ncx::DocumentWindow * window = app.windows().empty() ? nullptr : app.windows().front();
    if (!window || !WriteScreenshot(window, screenshot)) {
      std::fprintf(stderr, "NCX: couldn't write %s\n", screenshot.c_str());
      result = 1;
    }
  } else {
    result = app.run();
  }
  app.stop();
  return result;
}
