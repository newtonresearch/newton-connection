/*
 File: ui_test.cc

 The app's UI (PORTING.md Phase 7), a section a process (App is one for
 the process):
   shell   a document window for the Einstein backup (fixtures/cocoa): its
           title, its sidebar, the panes, the commands it allows; a window
           waiting for a Newton, closed again
   drop    files dropped: paths and file: URLs
   dock    the app against the fake Newton: it docks, its file is made
           and shown, Back Up (the command) backs it up, the window shows
           it, Disconnect ends the session quietly

   ncx_ui_test <section> <fixtures folder> <ncx-fake-newton>

 Run with NCX_PREFS=memory and NCX_DATA_DIR set (CMakeLists.txt): the
 user's preferences and backups stay out of it. Needs a display.
 */

#include "FakeProcess.h"

#include "base/Core.h"
#include "base/Threads.h"
#include "platform/AppFolders.h"
#include "ui/App.h"
#include "ui/DocumentWindow.h"
#include "ui/Drop.h"
#include "ui/panels/DevicePanel.h"

#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Tree.H>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>

using namespace ncx;

namespace {

int gFailed = 0;
std::filesystem::path gFixtures;
std::string gFakeNewton;

void Check(bool inOk, const std::string & inWhat)
{
  std::printf("%s %s\n", inOk ? "ok     " : "FAIL   ", inWhat.c_str());
  if (!inOk)
    ++gFailed;
}

/** A port for the dock section: below 40000 (Windows), apart from the
    other tests' (comms_test 10000 ... 19999, dock_test 21000 ... 39039). */
int Port()
{
  static int port = 39100 + (int)(std::random_device{}() % 400) * 2;
  return port;
}

void RunOnFLTKThread(std::function<void()> inTask)
{
  auto * task = new std::function<void()>(std::move(inTask));
  Fl::awake([](void * inData) {
    auto * task = static_cast<std::function<void()> *>(inData);
    (*task)();
    delete task;
  }, task);
}

/** Events until inDone says so, at most inSeconds; whether it did. */
bool WaitFor(double inSeconds, const std::function<bool()> & inDone)
{
  auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds((int)(inSeconds * 1000));
  while (!inDone()) {
    if (std::chrono::steady_clock::now() > end)
      return false;
    Fl::wait(0.05);
  }
  return true;
}

/** The labels of a tree item's children. */
std::vector<std::string> Children(Fl_Tree_Item * inItem)
{
  std::vector<std::string> labels;
  for (int i = 0; inItem && i < inItem->children(); ++i)
    labels.push_back(inItem->child(i)->label() ? inItem->child(i)->label() : "");
  return labels;
}

bool Has(const std::vector<std::string> & inList, const std::string & inItem)
{
  for (const std::string & item : inList)
    if (item == inItem)
      return true;
  return false;
}


/*-- shell --------------------------------------------------------------------*/

void TestShell()
{
  std::filesystem::path file = AppDataFolder() / "einstein.newtondevice";
  std::filesystem::copy_file(gFixtures / "cocoa" / "einstein.newtondevice", file,
                             std::filesystem::copy_options::overwrite_existing);
  App & app = App::get();
  App::Options options;
  options.dock = false;
  options.files = { file };
  app.start(options);
  Fl::check();
  Check(app.windows().size() == 1, "shell: a window for the file");
  if (app.windows().empty())
    return;
  DocumentWindow * window = app.windows().front();
  Check(window->file() == std::filesystem::absolute(file) && window->label()
        && std::strcmp(window->label(), "System Administrator") == 0, "shell: its file, titled by the owner's name");
  std::vector<std::string> top = Children(window->sidebar()->root());
  Check(top.size() == 3 && top[0] == "System Administrator" && top[1] == "Internal" && top[2] == "Card",
        "shell: the sidebar: the device, then the stores, internal first");
  Fl_Tree_Item * internal = window->sidebar()->find_item("Internal");
  std::vector<std::string> apps = Children(internal);
  Check(Has(apps, "Names") && Has(apps, "Notes") && Has(apps, "Packages"), "shell: the internal store's apps");
  Fl_Tree_Item * names = window->sidebar()->find_item("Internal/Names");
  Check(names && Has(Children(names), "Names"), "shell: an app's soups");
  Fl_Tree_Item * selected = window->sidebar()->first_selected_item();
  Check(selected && selected->label() && std::string(selected->label()) == "System Administrator",
        "shell: the device selected");

  // the device panel
  DevicePanel * device = window->devicePanel();
  Check(device->visible() && device->manufacturerText() == "Apple" && device->serialText() == "0000-4E65-7774-6F6E"
        && device->versionText() == "2.1 (717260)-1", "shell: the device panel: manufacturer, serial number, OS version");
  Check(device->backupText().find("This Newton device was backed up ") == 0, "shell: ... when it was backed up");
  Check(!device->mBackUp->active() && !device->mRestore->active(), "shell: ... not docked: no Back Up, no Restore");
  device->mSerialLabel->do_callback();
  device->mVersionLabel->do_callback();
  Check(device->serialText() == "233499568" && device->versionText() == "2.2",
        "shell: ... a click on a label: the Newton ID, the ROM version (" + device->serialText() + ", "
        + device->versionText() + ")");
  device->mSerialLabel->do_callback();
  Check(device->serialText() == "0000-4E65-7774-6F6E", "shell: ... and back");
  window->sidebar()->select_only(window->sidebar()->find_item("Internal"), 1);
  Check(!device->visible(), "shell: a store selected: no device panel");
  window->sidebar()->select_only(window->sidebar()->root()->child(0), 1);
  Check(device->visible(), "shell: the device again: its panel");

  Check(!app.canDo(Command::kBackUp, window) && !app.canDo(Command::kInstallPackage, window)
        && app.canDo(Command::kOpen, window) && app.canDo(Command::kToggleSidebar, window),
        "shell: not docked: no Back Up, no Install");
  bool sidebar = window->sidebarShown();
  app.command(Command::kToggleSidebar, window);
  Check(window->sidebarShown() != sidebar && window->sidebar()->visible() == !sidebar, "shell: the sidebar toggled");
  app.command(Command::kToggleSidebar, window);
  Check(window->sidebar()->visible() && window->sidebar()->w() > 0, "shell: ... and back");
  bool inspector = window->inspectorShown();
  app.command(Command::kToggleInspector, window);
  Check(window->inspectorShown() != inspector, "shell: the inspector toggled");

  // a window waiting for a Newton, closed again
  app.command(Command::kNewConnection, window);
  Fl::check();
  Check(app.windows().size() == 2 && app.windows().back()->file().empty(), "shell: New Connection: a waiting window");
  DocumentWindow * waiting = app.windows().back();
  Check(!app.canDo(Command::kToggleSidebar, waiting), "shell: it has no sidebar to show");
  app.command(Command::kClose, waiting);
  Fl::check();
  Check(app.windows().size() == 1, "shell: Close closes it");
  // the same file again: the same window
  app.openDocument(file);
  Check(app.windows().size() == 1, "shell: a file open already: its window");
  app.stop();
  Check(app.windows().empty(), "shell: the windows closed at the end");
}


/*-- drop ---------------------------------------------------------------------*/

/** A file: URL for a file (inHost: "" or "localhost"), spaces %-escaped. */
std::string FileURL(const std::filesystem::path & inFile, const std::string & inHost)
{
  std::string path = UTF8FromPath(inFile), url = "file://" + inHost;
  if (path.empty() || path[0] != '/')
    url += '/';                   // file:///C:/...
  for (char c : path)
    url += c == ' ' ? std::string("%20") : std::string(1, c == '\\' ? '/' : c);
  return url;
}

void TestDrop()
{
  std::filesystem::path folder = std::filesystem::absolute(AppDataFolder()) / "My Packages";
  std::filesystem::create_directories(folder);
  std::filesystem::path a = folder / "a b.newtonpkg", c = folder / "c.pkg";
  std::ofstream(a) << "package0";
  std::ofstream(c) << "package0";
  std::vector<std::filesystem::path> files
    = DroppedFiles(FileURL(a, "") + "\r\n" + UTF8FromPath(c) + "\n/no/such/file\n");
  Check(files.size() == 2 && files[0] == a && files[1] == c,
        "drop: a file: URL (%-escaped), a path; what isn't there left out (" + std::to_string(files.size()) + ")");
  files = DroppedFiles(FileURL(c, "localhost"));
  Check(files.size() == 1 && files[0] == c, "drop: file://localhost/...");
}


/*-- dock ---------------------------------------------------------------------*/

void TestDock()
{
  FakeProcess fake;
  fake.start(gFakeNewton, Port(), "--round 1");
  App & app = App::get();
  App::Options options;
  options.targets = { "tcp:" + std::to_string(Port()) };
  app.start(options);
  Check(app.windows().size() == 1 && app.windows().front()->file().empty(), "dock: a window waits for a Newton");
  DocumentWindow * window = app.windows().empty() ? nullptr : app.windows().front();
  bool docked = WaitFor(20, [&] { return app.docked() && window && !window->file().empty(); });
  Check(docked && app.windows().size() == 1, "dock: docked, and the waiting window shows its file");
  if (!docked) {
    app.stop();
    fake.finish();
    return;
  }
  Check(window->file().parent_path() == std::filesystem::absolute(AppDataFolder())
        && window->label() && std::strcmp(window->label(), "Ada Lovelace") == 0,
        "dock: its file in the app's folder, titled by the owner's name");
  Check(Has(Children(window->sidebar()->root()), "Keyboard") && Has(Children(window->sidebar()->root()), "Screenshot"),
        "dock: Keyboard and Screenshot while docked");
  Check(app.canDo(Command::kBackUp, window) && app.canDo(Command::kInstallPackage, window)
        && app.canDo(Command::kDisconnect, window), "dock: Back Up, Install, Disconnect allowed");

  app.command(Command::kBackUp, window);
  Check(!app.canDo(Command::kBackUp, window), "dock: no second Back Up while one runs");
  bool backedUp = WaitFor(30, [&] { return !app.dock().busy() && app.message().find("Backed up") == 0; });
  Check(backedUp && app.message().find("Backed up 4 soups: 5 entries came") == 0, "dock: Back Up: " + app.message());
  Check(std::string(window->messageText()).find("Backed up 4 soups") == 0, "dock: the window says so");
  Fl_Tree_Item * names = window->sidebar()->find_item("Internal/Names/Names");
  if (!names)
    for (const std::string & store : Children(window->sidebar()->root()))
      if (!names)
        names = window->sidebar()->find_item((store + "/Names/Names").c_str());
  Check(names != nullptr, "dock: the window shows the soups backed up");

  app.command(Command::kDisconnect, window);
  bool undocked = WaitFor(15, [&] { return !app.docked(); });
  Check(undocked && app.message().find("The connection ended") == std::string::npos, "dock: Disconnect, quietly");
  Check(undocked && !Has(Children(window->sidebar()->root()), "Keyboard"), "dock: no Keyboard once undocked");
  app.stop();
  fake.finish();
  Check(fake.why.empty() && !fake.received.empty() && fake.received.back() == "disc",
        "dock: the fake Newton saw it end with 'disc' " + fake.why);
}

} // namespace


int main(int argc, char * argv[])
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc != 4) {
    std::fprintf(stderr, "usage: ncx_ui_test shell|dock|drop <fixtures> <ncx-fake-newton>\n");
    return 2;
  }
  std::string section = argv[1];
  gFixtures = PathFromUTF8(argv[2]);
  gFakeNewton = argv[3];
  const char * prefs = std::getenv("NCX_PREFS");
  if (!prefs || std::strcmp(prefs, "memory") != 0 || !std::getenv("NCX_DATA_DIR")) {
    std::fprintf(stderr, "ncx_ui_test: NCX_PREFS=memory and NCX_DATA_DIR, please\n");
    return 2;
  }
  std::filesystem::remove_all(AppDataFolder());
  std::filesystem::create_directories(AppDataFolder());
  InitCore();
  Fl::lock();
  SetMainThreadRunner(RunOnFLTKThread);
  if (section == "shell")
    TestShell();
  else if (section == "dock")
    TestDock();
  else if (section == "drop")
    TestDrop();
  else {
    std::fprintf(stderr, "no section \"%s\"\n", section.c_str());
    return 2;
  }
  std::printf("%s\n", gFailed ? "FAILED" : "all ok");
  return gFailed ? 1 : 0;
}
