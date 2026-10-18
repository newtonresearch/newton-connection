/*
 File: ui_test.cc

 The app's UI (PORTING.md Phase 7), a section a process (App is one for
 the process):
   shell   a document window for the Einstein backup (fixtures/cocoa): its
           title, its sidebar, the panes, the commands it allows; a window
           waiting for a Newton, closed again
   drop    files dropped: paths and file: URLs; keys
   newton1 a NewtonOS 1 Newton docks: the app backs it up (the Newton 1
           menu's session)
   dock    the app against the fake Newton: it docks, its file is made
           and shown, Back Up (the command) backs it up, the window shows
           it, Disconnect ends the session quietly

   ncx_ui_test <section> <fixtures folder> <ncx-fake-newton>

 Run with NCX_PREFS=memory and NCX_DATA_DIR set (CMakeLists.txt): the
 user's preferences and backups stay out of it. Needs a display.
 */

#include "FakeProcess.h"

#include "base/Core.h"
#include "base/Prefs.h"
#include "base/Threads.h"
#include "dock/Keyboard.h"
#include "platform/AppFolders.h"
#include "ui/App.h"
#include "ui/DocumentWindow.h"
#include "ui/Drop.h"
#include "ui/Inspector.h"
#include "ui/Preferences.h"
#include "ui/RestoreDialog.h"
#include "model/DeviceFile.h"
#include "ui/panels/KeyboardPanel.h"
#include "ui/panels/PackageView.h"
#include "ui/panels/PassthroughArea.h"
#include "ui/panels/RichView.h"
#include "ui/panels/ScreenshotPanel.h"
#include "ui/panels/ChooserPanel.h"
#include "ui/panels/DevicePanel.h"
#include "ui/panels/SoupPanel.h"
#include "ui/panels/StorePanel.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Browser.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Tree.H>

#include <algorithm>
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
int gAlerts = 0;
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

/** The window as a PNG in the test's folder (to look at). */
void Snapshot(Fl_Window * inWindow, const std::string & inName)
{
  Fl::check();
  Fl_Image_Surface surface(inWindow->w(), inWindow->h());
  Fl_Surface_Device::push_current(&surface);
  surface.draw(inWindow);
  Fl_RGB_Image * image = surface.image();
  Fl_Surface_Device::pop_current();
  if (image)
    fl_write_png(UTF8FromPath(AppDataFolder() / (inName + ".png")).c_str(), image);
  delete image;
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
  Snapshot(window, "device");
  window->sidebar()->select_only(window->sidebar()->find_item("Internal"), 1);
  StorePanel * store = window->storePanel();
  Check(!device->visible() && store->visible(), "shell: a store selected: its panel");
  Check(std::string(store->mKind->label()) == "Internal" && std::string(store->mStatus->label()) == "Default store"
        && std::string(store->mCapacity->label()).find("K  (") != std::string::npos
        && store->mLevel->value() > 0 && store->mLevel->value() <= 100,
        "shell: ... kind, status, capacity, how full (" + std::string(store->mCapacity->label()) + ", "
        + std::string(store->mUsed->label()) + ")");
  Snapshot(window, "store");
  window->sidebar()->select_only(window->sidebar()->find_item("Card"), 1);
  Check(store->visible() && std::string(store->mKind->label()) != "Internal", "shell: the card: " + std::string(store->mKind->label())
        + ", " + store->mStatus->label());
  // the soup panel: Names
  window->sidebar()->select_only(window->sidebar()->find_item("Internal/Names/Names"), 1);
  SoupPanel * soup = window->soupPanel();
  bool read = WaitFor(10, [&] { return !soup->loading(); });
  const SoupData & namesData = soup->data();
  std::vector<std::string> headers;
  for (const SoupData::Column & column : namesData.columns)
    headers.push_back(column.header);
  Check(read && soup->visible() && !store->visible() && namesData.error.empty()
        && headers == std::vector<std::string>({ "ID", "Name", "Location", "Folder", "Modified" }),
        "shell: the soup panel: Names, its columns " + namesData.error);
  Check(namesData.rows.size() == 9, "shell: ... its entries (" + std::to_string(namesData.rows.size()) + ")");
  soup->sortBy(1, true);
  bool sorted = true;
  for (size_t r = 1; r < namesData.rows.size(); ++r)
    sorted = sorted && !(soup->cellText((int)r, 1) < soup->cellText((int)r - 1, 1)) ;
  soup->sortBy(1, false);
  Check(sorted && soup->cellText(0, 1) >= soup->cellText((int)namesData.rows.size() - 1, 1),
        "shell: ... sorted by name, both ways (" + soup->cellText(0, 1) + " ... "
        + soup->cellText((int)namesData.rows.size() - 1, 1) + ")");
  soup->selectRow(0, true);
  soup->selectRow(2, true);
  std::vector<int64_t> chosen = soup->selectedEntries();
  soup->sortBy(0, true);
  std::vector<int64_t> after = soup->selectedEntries();
  Check(chosen.size() == 2 && after.size() == 2 && std::is_permutation(chosen.begin(), chosen.end(), after.begin()),
        "shell: ... two selected, still after sorting again");
  Snapshot(window, "soup");

  // Delete, not docked: from the file only (the warning silenced)
  Prefs::App().setBool(pref::kNoDeleteWarning, true);
  Check(app.canDo(Command::kDelete, window) && app.canDo(Command::kExport, window), "shell: Delete and Export allowed");
  app.command(Command::kDelete, window);
  bool deleted = WaitFor(10, [&] { return app.message().find("Deleted") == 0 && !soup->loading() && soup->data().rows.size() == 7; });
  Check(deleted && app.messageFile() == window->file(), "shell: Delete: two entries left the file: " + app.message()
        + " (" + std::to_string(soup->data().rows.size()) + ")");
  Check(std::string(window->messageText()).find("Deleted 2 entries") == 0, "shell: ... the window says so");
  // Export: all of the soup (none selected)
  std::filesystem::path exported = AppDataFolder() / "exported";
  std::filesystem::create_directories(exported);
  window->exportEntriesTo(exported);
  bool exportedAll = WaitFor(20, [&] { return app.message().find("Exported") == 0; });
  size_t files = 0;
  for (const auto & each : std::filesystem::directory_iterator(exported))
    files += each.path().extension() == ".vcf" ? 1 : 0;
  Check(exportedAll && app.message().find("Exported 7 entries") == 0 && files > 0,
        "shell: Export: " + app.message());
  app.command(Command::kSelectAll, window);
  Check(soup->selectedEntries().size() == 7, "shell: Select All");
  // Import, not docked: into the file (the warning silenced)
  Prefs::App().setBool(pref::kNoAddWarning, true);
  Check(app.canDo(Command::kImport, window), "shell: Import allowed");
  window->importFiles({ gFixtures / "cocoa" / "names-person.vcf" });
  bool imported = WaitFor(10, [&] { return app.message().find("Imported") == 0 && !soup->loading()
                                           && soup->data().rows.size() > 7; });
  Check(imported, "shell: Import: a vCard into Names: " + app.message() + " ("
        + std::to_string(soup->data().rows.size()) + ")");
  // packages leave only from the Newton
  window->sidebar()->select_only(window->sidebar()->find_item("Internal/Packages/Packages"), 1);
  WaitFor(10, [&] { return !soup->loading(); });
  app.command(Command::kSelectAll, window);
  Check(!soup->selectedEntries().empty() && !app.canDo(Command::kDelete, window),
        "shell: not docked, packages can't be deleted");

  // the inspector: a package's info
  if (!window->inspectorShown())
    app.command(Command::kToggleInspector, window);
  Inspector * inspector = window->inspector();
  bool inspected = WaitFor(10, [&] { return !inspector->loading() && inspector->status().empty(); });
  std::vector<std::string> rows = inspector->packageView()->rowTexts();
  std::string all;
  for (const std::string & row : rows)
    all += row + "; ";
  Check(inspected && inspector->packageView()->visible() && inspector->packageView()->info().error.empty()
        && all.find("Name: hello:SIG") != std::string::npos && all.find("Part 0  (form)") != std::string::npos,
        "shell: the inspector: the package's info: " + all + inspector->packageView()->info().error);
  Snapshot(window, "package");
  soup->selectRow(0, false);
  Check(inspector->status() == "No selection", "shell: the inspector: no selection");
  window->sidebar()->select_only(window->sidebar()->find_item("Internal/Names/Names"), 1);
  WaitFor(10, [&] { return !soup->loading(); });
  soup->selectRow(0, true);
  soup->selectRow(1, true);
  Check(inspector->status() == "Multiple selection", "shell: the inspector: multiple selection");
  // an entry as its translator exports it
  soup->selectRow(1, false);
  bool shown = WaitFor(10, [&] { return !inspector->loading() && inspector->status().empty(); });
  Check(shown && inspector->richView()->visible() && inspector->richView()->text().find("BEGIN:VCARD") != std::string::npos,
        "shell: the inspector: a card, as vCard " + inspector->status());
  // notes and Works: rich text, pictures in place
  int pictures = 0;
  std::string texts;
  for (const char * path : { "Internal/Notes/Notes", "Internal/Newton Works/NewtWorks" }) {
    window->sidebar()->select_only(window->sidebar()->find_item(path), 1);
    WaitFor(10, [&] { return !soup->loading(); });
    for (int row = 0; row < (int)soup->data().rows.size(); ++row) {
      soup->clearSelection();
      soup->selectRow(row, true);
      WaitFor(10, [&] { return !inspector->loading(); });
      pictures += inspector->richView()->pictures();
      for (char c : inspector->richView()->text().substr(0, 20))
        texts += c == '\n' ? '/' : c;
      texts += " | ";
      if (inspector->richView()->pictures() > 0)
        Snapshot(window, "inspector-picture");
    }
  }
  Check(pictures > 0 && texts.size() > 20, "shell: the inspector: notes and Works, " + std::to_string(pictures)
        + " pictures: " + texts);
  window->sidebar()->select_only(window->sidebar()->find_item("Internal"), 1);
  Check(inspector->status() == "Inspector not applicable", "shell: the inspector: a store: not applicable");

  window->sidebar()->select_only(window->sidebar()->root()->child(0), 1);
  Check(device->visible() && !store->visible() && !soup->visible(), "shell: the device again: its panel");

  Check(!app.canDo(Command::kBackUp, window) && !app.canDo(Command::kInstallPackage, window)
        && app.canDo(Command::kOpen, window) && app.canDo(Command::kToggleSidebar, window),
        "shell: not docked: no Back Up, no Install");
  bool sidebar = window->sidebarShown();
  app.command(Command::kToggleSidebar, window);
  Check(window->sidebarShown() != sidebar && window->sidebar()->visible() == !sidebar, "shell: the sidebar toggled");
  app.command(Command::kToggleSidebar, window);
  Check(window->sidebar()->visible() && window->sidebar()->w() > 0, "shell: ... and back");
  bool inspectorShown = window->inspectorShown();
  app.command(Command::kToggleInspector, window);
  Check(window->inspectorShown() != inspectorShown, "shell: the inspector toggled");

  // the preferences window: the preferences in, and out again
  Prefs::App().setInt(pref::kTCPIPPort, 3700);
  Prefs::App().setBool(pref::kSetNewtonTime, true);
  PreferencesWindow & prefs = PreferencesWindow::Show();
  Fl::check();
  Check(prefs.shown() && std::string(prefs.mTCPPort->value()) == "3700" && prefs.mSetTime->value() == 1
        && prefs.mBaud->value() == 0 && prefs.mSerialPort->value() == 0, "shell: preferences: shown as they are");
  Snapshot(&prefs, "preferences");
  prefs.mTCPPort->value("3800");
  prefs.mAutoBackup->value(1);
  prefs.mBaud->value(2);
  prefs.mPassword->value("secret");
  prefs.save();
  prefs.hide();
  Check(Prefs::App().tcpPort() == 3800 && Prefs::App().getBool(pref::kAutoBackup) && Prefs::App().baudRate() == 115200
        && Prefs::App().getString(pref::kPassword) == "secret", "shell: preferences: saved");
  Prefs::App().setString(pref::kSerialPort, "/dev/ttyNewton");
  std::vector<std::string> targets = AppDock::DefaultTargets();
  Check(targets.size() >= 2 && targets[0] == "tcp-all:3800" && targets[1] == "serial:/dev/ttyNewton@115200",
        "shell: preferences: where the app listens (" + targets[0] + ", " + (targets.size() > 1 ? targets[1] : "") + ")");
  prefs.load();
  Check(prefs.mSerialPort->value() > 0, "shell: preferences: a serial port that isn't there is kept");
  Prefs::App().remove(pref::kSerialPort);
  Prefs::App().setBool(pref::kAutoBackup, false);

  // the restore dialog, with what the backup has for its own stores
  {
    std::string error;
    std::unique_ptr<DeviceFile> backup = DeviceFile::Open(file, DeviceFile::kReadOnly, error);
    std::vector<RestoreChoice> choices = backup ? RestoreChoices(*backup, backup->stores()) : std::vector<RestoreChoice>();
    Check(!choices.empty() && !choices[0].sources.empty(), "shell: restore: choices for the backup's stores " + error);
    if (!choices.empty() && !choices[0].sources.empty()) {
      RestoreDialog dialog(choices);
      RestoreSelection chosen = dialog.selection();
      Check(chosen.target.name == "Internal" && chosen.apps.size() == choices[0].sources[0].apps.size()
            && chosen.packages.empty(), "shell: restore: all apps at first, no packages ("
            + std::to_string(chosen.apps.size()) + " apps)");
      dialog.mAllPackages->do_callback();
      dialog.mApps->checked(1, 0);
      chosen = dialog.selection();
      Check(chosen.packages.size() == choices[0].sources[0].packages.size() && !chosen.packages.empty()
            && chosen.apps.size() + 1 == choices[0].sources[0].apps.size(), "shell: restore: packages chosen, an app not");
      dialog.show();
      Snapshot(&dialog, "restore");
      dialog.hide();
    }
  }

  // a window waiting for a Newton, closed again
  app.command(Command::kNewConnection, window);
  Fl::check();
  Check(app.windows().size() == 2 && app.windows().back()->file().empty(), "shell: New Connection: a waiting window");
  DocumentWindow * waiting = app.windows().back();
  Check(!app.canDo(Command::kToggleSidebar, waiting), "shell: it has no sidebar to show");
  Check(waiting->chooserPanel()->visible() && waiting->chooserPanel()->backups().size() == 1
        && waiting->chooserPanel()->backups()[0].newtonName == "System Administrator",
        "shell: it lists the backups in the app's folder (" + std::to_string(waiting->chooserPanel()->backups().size()) + ")");
  Snapshot(waiting, "chooser");
  app.command(Command::kClose, waiting);
  Fl::check();
  Check(app.windows().size() == 1, "shell: Close closes it");
  // entries dragged from this window to another document's (D9)
  {
    std::filesystem::path other = AppDataFolder() / "other.newtondevice";
    std::filesystem::copy_file(gFixtures / "cocoa" / "einstein.newtondevice", other,
                               std::filesystem::copy_options::overwrite_existing);
    DocumentWindow * second = app.openDocument(other);
    window->sidebar()->select_only(window->sidebar()->find_item("Internal/Names/Names"), 1);
    WaitFor(10, [&] { return !soup->loading(); });
    soup->selectRow(0, true);
    soup->selectRow(1, true);
    std::vector<int64_t> dragged = soup->selectedEntries();
    Check(second && dragged.size() == 2, "shell: a second document, two Names entries to drag");
    if (second) {
      second->sidebar()->select_only(second->sidebar()->find_item("Internal/Names/Names"), 1);
      WaitFor(10, [&] { return !second->soupPanel()->loading(); });
      size_t before = second->soupPanel()->data().rows.size();
      second->dropped(EntriesPayload(window->file(), dragged));
      bool copied = WaitFor(10, [&] { return app.message().find("Copied") == 0 && !second->soupPanel()->loading()
                                             && second->soupPanel()->data().rows.size() == before + 2; });
      Check(copied && app.messageFile() == second->file(), "shell: dropped on it: copied into its Names: " + app.message());
      // a file dropped: imported
      second->dropped(UTF8FromPath(gFixtures / "cocoa" / "names-person.vcf"));
      bool imported = WaitFor(10, [&] { return app.message().find("Imported") == 0; });
      Check(imported, "shell: a file dropped on it: imported: " + app.message());
      app.command(Command::kClose, second);
      Fl::check();
    }
  }

  // the same file again: the same window
  app.openDocument(file);
  Check(app.windows().size() == 1, "shell: a file open already: its window");
#if !defined(__APPLE__)
  // the last document closed: the chooser again
  app.command(Command::kClose, window);
  Fl::check();
  Check(app.windows().size() == 1 && app.windows().front()->file().empty(),
        "shell: the last document closed: the window waiting for a Newton again");
#endif
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
  // entries dragged between windows
  std::filesystem::path from;
  std::vector<int64_t> pks;
  Check(ParseEntriesPayload(EntriesPayload(c, { 3, 14, 15 }), from, pks) && from == c
        && pks == std::vector<int64_t>({ 3, 14, 15 }) && !ParseEntriesPayload(FileURL(c, ""), from, pks),
        "drop: entries dragged between windows: the payload");
  // keys for the keyboard panel
  Check(NewtonKeyFor(FL_BackSpace) == newtonkey::kBackspace && NewtonKeyFor(FL_Enter) == newtonkey::kReturn
        && NewtonKeyFor(FL_Up) == newtonkey::kUp && NewtonKeyFor(FL_F + 3) == newtonkey::kF1 + 2
        && NewtonKeyFor('a') == 0, "keys: the Newton's characters for keys that aren't text");
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
  if (names) {
    // the entries' texts come from the dock thread
    window->sidebar()->select_only(names, 1);
    bool read = WaitFor(10, [&] { return !window->soupPanel()->loading(); });
    const SoupData & data = window->soupPanel()->data();
    Check(read && data.rows.size() == 2 && data.rows[0].cells.size() > 1 && data.rows[0].cells[1] == "Ada Lovelace",
          "dock: the Names soup's entries, read on the dock thread ("
          + std::to_string(data.rows.size()) + ") " + data.error);
    // Delete, docked: on the Newton and from the file
    window->soupPanel()->selectRow(0, true);
    Check(app.canDo(Command::kDelete, window), "dock: Delete allowed");
    app.command(Command::kDelete, window);
    bool deleted = WaitFor(15, [&] { return !app.dock().busy() && app.message().find("Deleted") == 0
                                            && !window->soupPanel()->loading()
                                            && window->soupPanel()->data().rows.size() == 1; });
    Check(deleted, "dock: Delete: on the Newton and from the file: " + app.message());
    // Import, docked: into the file, and to the Newton
    window->importFiles({ gFixtures / "cocoa" / "names-person.vcf" });
    bool imported = WaitFor(15, [&] { return !app.dock().busy() && app.message().find("Imported") == 0; });
    Check(imported && app.message().find("sent to the Newton") != std::string::npos,
          "dock: Import: into the file and to the Newton: " + app.message());
  }

  // Restore: what to restore (all apps, as the dialog has it), restored
  app.setRestoreChooser([](const std::vector<RestoreChoice> & inChoices) -> std::optional<RestoreSelection> {
    if (inChoices.empty() || inChoices[0].sources.empty())
      return std::nullopt;
    RestoreSelection selection;
    selection.target = inChoices[0].store;
    selection.source = inChoices[0].sources[0].store;
    selection.apps = inChoices[0].sources[0].apps;
    return selection;
  });
  Check(app.canDo(Command::kRestore, window), "dock: Restore allowed");
  app.command(Command::kRestore, window);
  bool restored = WaitFor(20, [&] { return !app.dock().busy() && app.message().find("Restored") == 0; });
  Check(restored, "dock: Restore: " + app.message());

  // Dump ROM (16 KB of it), Dump Card
  Check(app.canDo(Command::kDumpROM, window) && app.canDo(Command::kDumpCard, window), "dock: the dumps allowed");
  Prefs::App().setInt(pref::kROMSize, 16384);
  std::filesystem::path rom = AppDataFolder() / "rom.bin";
  app.dock().dumpROM(rom, false);
  bool dumped = WaitFor(20, [&] { return !app.dock().busy() && app.message().find("Read the ROM") == 0; });
  std::error_code sizeError;
  Check(dumped && std::filesystem::file_size(rom, sizeError) == 16384, "dock: Dump ROM: " + app.message());
  // a failure is told (an alert): a ROM file that can't be made
  int alerts = gAlerts;
  app.dock().dumpROM(rom / "inside-a-file.bin", false);
  bool told = WaitFor(20, [&] { return !app.dock().busy() && gAlerts > alerts; });
  Check(told && app.message().find("The ROM isn't complete") == 0, "dock: a failure is told: " + app.message());
  gAlerts = alerts;
  Prefs::App().remove(pref::kROMSize);
  std::filesystem::path cards = AppDataFolder() / "cards";
  app.dock().dumpCards(cards);
  bool cardsRead = WaitFor(30, [&] { return !app.dock().busy() && app.message().find("Read 1 card") == 0; });
  Check(cardsRead, "dock: Dump Card: " + app.message());

  // the screenshot panel: screenshot mode, a shot, saved
  window->sidebar()->select_only(window->sidebar()->find_item("Screenshot"), 1);
  ScreenshotPanel * screenshot = window->screenshotPanel();
  bool ready = WaitFor(15, [&] { return screenshot->isReady(); });
  Check(ready && screenshot->visible() && screenshot->active() && screenshot->mTake->active(),
        "dock: the screenshot panel: the Newton is ready: " + screenshot->instructions());
  screenshot->mTake->do_callback();
  bool taken = WaitFor(20, [&] { return screenshot->shot().width > 0 && !app.dock().busy(); });
  Check(taken && screenshot->shot().width == 320 && screenshot->shot().height == 480 && screenshot->mSave->active(),
        "dock: ... Take Screenshot: " + std::to_string(screenshot->shot().width) + " x "
        + std::to_string(screenshot->shot().height));
  std::filesystem::path png = AppDataFolder() / "screenshot.png";
  Check(screenshot->save(png) && std::filesystem::file_size(png) > 100, "dock: ... saved as PNG");
  Snapshot(window, "screenshot");
  window->sidebar()->select_only(window->sidebar()->root()->child(0), 1);
  Check(!screenshot->active(), "dock: ... another panel: screenshot mode ends");

  // the keyboard panel: passthrough on, text pasted (as a drop or Edit > Paste
  // brings it) goes to the Newton
  window->sidebar()->select_only(window->sidebar()->find_item("Keyboard"), 1);
  KeyboardPanel * keyboard = window->keyboardPanel();
  bool on = WaitFor(15, [&] { return keyboard->on(); });
  Check(on && keyboard->visible() && app.canDo(Command::kPaste, window),
        "dock: the keyboard panel: passthrough on: " + keyboard->instructions());
  std::string hello = "Hello";
  Fl::e_text = &hello[0];
  Fl::e_length = (int)hello.size();
  keyboard->mArea->handle(FL_PASTE);
  window->sidebar()->select_only(window->sidebar()->root()->child(0), 1);
  Check(!keyboard->active() && !keyboard->on(), "dock: ... another panel: passthrough ends");

  app.command(Command::kDisconnect, window);
  bool undocked = WaitFor(15, [&] { return !app.docked(); });
  Check(undocked && app.message().find("The connection ended") == std::string::npos, "dock: Disconnect, quietly");
  Check(undocked && !Has(Children(window->sidebar()->root()), "Keyboard"), "dock: no Keyboard once undocked");
  app.stop();
  fake.finish();
  Check(fake.deleted == std::vector<int64_t>({ 100 }), "dock: the fake Newton deleted Ada (100)");
  Check(!fake.added.empty(), "dock: the fake Newton got the entries imported (" + std::to_string(fake.added.size()) + ")");
  Check(!fake.restored.empty(), "dock: the fake Newton was restored (" + std::to_string(fake.restored.size()) + ")");
  bool typed = false;
  for (const std::string & each : fake.typed)
    typed = typed || each.find("Hello") != std::string::npos;
  Check(typed, "dock: the fake Newton got the text typed (" + std::to_string(fake.typed.size()) + ")");
  Check(fake.why.empty() && !fake.received.empty() && fake.received.back() == "disc",
        "dock: the fake Newton saw it end with 'disc' " + fake.why);
}

/*-- newton1 ------------------------------------------------------------------*/

void TestNewton1()
{
  // NewtonOS 1 docks for what the Newton 1 menu says: Sync (the default)
  FakeProcess fake;
  fake.start(gFakeNewton, Port() + 1, "--newton1");
  App & app = App::get();
  App::Options options;
  options.targets = { "tcp:" + std::to_string(Port() + 1) };
  app.start(options);
  Check(app.isChecked(Command::kNewton1Sync, nullptr) && !app.isChecked(Command::kNewton1Restore, nullptr),
        "newton1: Sync is the session at first");
  bool backedUp = WaitFor(30, [&] { return app.message().find("Backed up") == 0; });
  Check(backedUp && app.windows().size() == 1 && !app.windows().front()->file().empty(),
        "newton1: docked and backed up by itself: " + app.message());
  bool undocked = WaitFor(15, [&] { return !app.docked(); });
  Check(undocked, "newton1: the session ended after the backup: " + app.dockStatus() + "; " + app.message());
  app.stop();
  fake.finish();
  Check(fake.why.empty(), "newton1: the fake Newton is content " + fake.why);
  app.command(Command::kNewton1Restore, nullptr);
  Check(app.isChecked(Command::kNewton1Restore, nullptr) && !app.isChecked(Command::kNewton1Sync, nullptr),
        "newton1: the menu chooses the session");
}

} // namespace


int main(int argc, char * argv[])
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc != 4) {
    std::fprintf(stderr, "usage: ncx_ui_test shell|dock|drop|newton1 <fixtures> <ncx-fake-newton>\n");
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
  // errors are told in the output, not in alerts that wait for a click
  App::get().setAlert([](const std::string & inText) { std::printf("        alert: %s\n", inText.c_str()); ++gAlerts; });
  if (section == "shell")
    TestShell();
  else if (section == "dock")
    TestDock();
  else if (section == "drop")
    TestDrop();
  else if (section == "newton1")
    TestNewton1();
  else {
    std::fprintf(stderr, "no section \"%s\"\n", section.c_str());
    return 2;
  }
  Check(gAlerts == 0, "no alerts (" + std::to_string(gAlerts) + ")");
  std::printf("%s\n", gFailed ? "FAILED" : "all ok");
  return gFailed ? 1 : 0;
}
