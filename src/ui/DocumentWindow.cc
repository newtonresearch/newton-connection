/*
 File: DocumentWindow.cc

 A document window (DocumentWindow.h).
 */

#include "ui/DocumentWindow.h"
#include "ui/App.h"
#include "ui/Commands.h"
#include "ui/Dialogs.h"
#include "ui/Drop.h"
#include "ui/Edits.h"
#include "ui/Images.h"
#include "ui/Inspector.h"
#include "ui/panels/ChooserPanel.h"
#include "ui/panels/DevicePanel.h"
#include "ui/panels/KeyboardPanel.h"
#include "ui/panels/ScreenshotPanel.h"
#include "ui/panels/SoupPanel.h"
#include "ui/panels/StorePanel.h"

#include "base/Dates.h"
#include "base/Prefs.h"
#include "base/Threads.h"
#include "dock/Packages.h"
#include "base/Version.h"
#include "model/DeviceFile.h"
#include "platform/AppFolders.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Tile.H>
#include <FL/Fl_Toggle_Button.H>
#include <FL/Fl_Tree.H>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace ncx {

namespace {

const int kMenuHeight = 25;
const int kToolbarHeight = 34;
const int kStatusHeight = 22;
const int kMinContentWidth = 240;
const int kMargin = 12;

// the window's size and panes, as the last one closed had them
const char * const kPrefWidth = "WindowWidth";
const char * const kPrefHeight = "WindowHeight";
const char * const kPrefSidebarWidth = "SidebarWidth";
const char * const kPrefInspectorWidth = "InspectorWidth";
const char * const kPrefSidebarShown = "SidebarShown";
const char * const kPrefInspectorShown = "InspectorShown";

/** A label shows the text as it is (FLTK draws "@..." as symbols). */
std::string LabelText(const std::string & inText)
{
  std::string text;
  for (char c : inText) {
    text += c;
    if (c == '@')
      text += '@';
  }
  return text;
}

} // namespace


/** What a sidebar item stands for. */
struct DocumentWindow::Node
{
  enum Kind { kDevice, kKeyboard, kScreenshot, kStore, kApp, kSoup };
  Kind kind;
  int64_t storePk = 0;
  int64_t appPk = 0;
  int64_t soupPk = 0;
};


DocumentWindow::DocumentWindow()
  : Fl_Double_Window(Prefs::App().getInt(kPrefWidth, 820), Prefs::App().getInt(kPrefHeight, 520), NCX_NAME)
{
  Prefs & prefs = Prefs::App();
  mSidebarWidth = std::max(120, prefs.getInt(kPrefSidebarWidth, mSidebarWidth));
  mInspectorWidth = std::max(120, prefs.getInt(kPrefInspectorWidth, mInspectorWidth));
  mSidebarShown = prefs.getBool(kPrefSidebarShown, true);
  mInspectorShown = prefs.getBool(kPrefInspectorShown, false);

  int top = 0;
#if !defined(__APPLE__)
  // D9: a menu bar in every document window (macOS: the app's, at the top
  // of the screen)
  Fl_Menu_Bar * bar = new Fl_Menu_Bar(0, 0, w(), kMenuHeight);
  BuildMenu(*bar);
  mMenuBar = bar;
  top = kMenuHeight;
#endif

  mToolbar = new Fl_Group(0, top, w(), kToolbarHeight);
  mToolbar->box(FL_FLAT_BOX);
  mSidebarButton = new Fl_Toggle_Button(6, top + 5, 32, kToolbarHeight - 10, "@menu");
  mSidebarButton->tooltip("Show or hide the sidebar");
  mSidebarButton->callback(ToggleCB, (void *)(intptr_t)Command::kToggleSidebar);
  mSidebarButton->clear_visible_focus();
  mActivity = new Fl_Box(46, top + 5, w() - 46 - 260, kToolbarHeight - 10);
  mActivity->align(FL_ALIGN_INSIDE | FL_ALIGN_RIGHT | FL_ALIGN_CLIP);
  mProgress = new Fl_Progress(w() - 210, top + 9, 120, kToolbarHeight - 18);
  mProgress->selection_color(FL_SELECTION_COLOR);
  mCancel = new Fl_Button(w() - 84, top + 5, 38, kToolbarHeight - 10, "@1+");
  mCancel->tooltip("Cancel");
  mCancel->callback(CancelCB);
  mCancel->clear_visible_focus();
  mInspectorButton = new Fl_Toggle_Button(w() - 38, top + 5, 32, kToolbarHeight - 10, "@search");
  mInspectorButton->tooltip("Show or hide the inspector");
  mInspectorButton->callback(ToggleCB, (void *)(intptr_t)Command::kToggleInspector);
  mInspectorButton->clear_visible_focus();
  mToolbar->resizable(mActivity);
  mToolbar->end();
  top += kToolbarHeight;

  int tileHeight = h() - top - kStatusHeight;
  mTile = new Fl_Tile(0, top, w(), tileHeight);
  mSidebar = new Fl_Tree(0, top, mSidebarWidth, tileHeight);
  mSidebar->showroot(0);
  mSidebar->selectmode(FL_TREE_SELECT_SINGLE);
  mSidebar->connectorstyle(FL_TREE_CONNECTOR_NONE);
  mSidebar->callback(TreeCB, this);
  mContent = new Fl_Group(mSidebarWidth, top, w() - mSidebarWidth, tileHeight);
  mContent->box(FL_FLAT_BOX);
  mContent->color(FL_BACKGROUND2_COLOR);
  mInfo = new Fl_Box(mContent->x() + kMargin, top + kMargin, mContent->w() - 2 * kMargin, tileHeight - 2 * kMargin);
  mInfo->align(FL_ALIGN_INSIDE | FL_ALIGN_TOP_LEFT | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
  mDevicePanel = new DevicePanel(mContent->x(), top, mContent->w(), tileHeight);
  mDevicePanel->hide();
  mStorePanel = new StorePanel(mContent->x(), top, mContent->w(), tileHeight);
  mStorePanel->hide();
  mSoupPanel = new SoupPanel(mContent->x(), top, mContent->w(), tileHeight);
  mSoupPanel->hide();
  mScreenshotPanel = new ScreenshotPanel(mContent->x(), top, mContent->w(), tileHeight);
  mScreenshotPanel->hide();
  mKeyboardPanel = new KeyboardPanel(mContent->x(), top, mContent->w(), tileHeight);
  mKeyboardPanel->hide();
  mChooserPanel = new ChooserPanel(mContent->x(), top, mContent->w(), tileHeight);
  mChooserPanel->hide();
  mSoupPanel->setSelectionChanged([this] {
    App::get().selectionChanged();
    inspectSelection();
  });
  mContent->resizable(mInfo);
  mContent->end();
  mInspector = new Inspector(w() - mInspectorWidth, top, mInspectorWidth, tileHeight);
  mTile->resizable(mContent);
  mTile->end();
  top += tileHeight;

  Fl_Group * status = new Fl_Group(0, top, w(), kStatusHeight);
  mMessage = new Fl_Box(6, top, w() / 2, kStatusHeight);
  mMessage->align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT | FL_ALIGN_CLIP);
  mMessage->labelsize(12);
  mDockState = new Fl_Box(w() / 2 + 6, top, w() / 2 - 12, kStatusHeight);
  mDockState->align(FL_ALIGN_INSIDE | FL_ALIGN_RIGHT | FL_ALIGN_CLIP);
  mDockState->labelsize(12);
  status->resizable(mMessage);
  status->end();

  resizable(mTile);
  size_range(kMinContentWidth + 200, 300);
  end();
  layoutTile();
  fillTree();
  updateState();
}


DocumentWindow::~DocumentWindow()
{
  Fl::remove_timeout(ProgressTimer, this);
}


bool DocumentWindow::bind(const std::filesystem::path & inFile, std::string & outError)
{
  std::unique_ptr<DeviceFile> file = DeviceFile::Open(inFile, DeviceFile::kReadOnly, outError);
  if (!file)
    return false;
  mDeviceFile = std::move(file);
  mFile = inFile;
  {
    std::string ignored;
    mWritable = DeviceFile::Open(inFile, DeviceFile::kReadWrite, ignored) != nullptr;
  }
  std::string name = mDeviceFile->newtonName();
  mTitle = name.empty() ? UTF8FromPath(inFile.stem()) : name;
  copy_label(mTitle.c_str());
  layoutTile();
  fillTree();
  return true;
}


void DocumentWindow::reload()
{
  if (mFile.empty())
    return;
  // a new connection sees what the dock thread wrote
  std::string error;
  std::unique_ptr<DeviceFile> file = DeviceFile::Open(mFile, DeviceFile::kReadOnly, error);
  if (file)
    mDeviceFile = std::move(file);
  fillTree();
}


const char * DocumentWindow::messageText() const
{
  return mMessage->label() ? mMessage->label() : "";
}


void DocumentWindow::hide()
{
  bool shown = this->shown();
  Fl_Double_Window::hide();
  if (shown) {
    if (mScreenshotPanel->active())
      mScreenshotPanel->leave();
    if (mKeyboardPanel->active())
      mKeyboardPanel->leave();
    rememberLayout();
    App::get().windowClosed(this);
  }
}


/*-- the sidebar --------------------------------------------------------------*/

void DocumentWindow::fillTree()
{
  // keep the selection where it can be
  std::string selected;
  if (Fl_Tree_Item * item = mSidebar->first_selected_item()) {
    char path[512];
    if (mSidebar->item_pathname(path, sizeof(path), item) == 0)
      selected = path;
  }
  mSidebar->clear_children(mSidebar->root());     // (clear() deletes the root too)
  mNodes.clear();
  App & app = App::get();
  mShowsDockItems = app.isDockedWindow(this) && !app.dock().newton1();
  auto add = [this](Fl_Tree_Item * inParent, const std::string & inLabel, Node inNode, const char * inIcon) {
    Fl_Tree_Item * item = mSidebar->add(inParent ? inParent : mSidebar->root(), inLabel.c_str());
    mNodes.emplace_back(new Node(inNode));
    item->user_data(mNodes.back().get());
    item->usericon(BuiltinImage(inIcon));
    return item;
  };
  if (mDeviceFile) {
    std::string name = mDeviceFile->newtonName();
    add(nullptr, name.empty() ? std::string("Newton") : name, Node { Node::kDevice }, "source-device");
    if (mShowsDockItems) {
      add(nullptr, "Keyboard", Node { Node::kKeyboard }, "source-keyboard");
      add(nullptr, "Screenshot", Node { Node::kScreenshot }, "source-display");
    }
    try {
      std::vector<StoreRecord> stores = mDeviceFile->stores();
      SortStores(stores);
      std::vector<SoupRecord> soups = mDeviceFile->soups();
      for (const StoreRecord & store : stores) {
        Fl_Tree_Item * storeItem = add(nullptr, store.name, Node { Node::kStore, store.pk },
                                       IsInternal(store) ? "source-internal" : "source-store");
        for (const AppRecord & appRecord : mDeviceFile->storeApps(store.pk, true)) {
          Fl_Tree_Item * appItem = add(storeItem, appRecord.name, Node { Node::kApp, store.pk, appRecord.pk }, "source-app");
          for (const SoupRecord & soup : soups)
            if (soup.storePk == store.pk && soup.appPk == appRecord.pk)
              add(appItem, soup.name, Node { Node::kSoup, store.pk, appRecord.pk, soup.pk }, "source-soup");
          appItem->close();
        }
      }
    } catch (const ModelError & error) {
      mInfo->copy_label(LabelText(error.what()).c_str());
    }
  }
  Fl_Tree_Item * item = selected.empty() ? nullptr : mSidebar->find_item(selected.c_str());
  if (!item && mSidebar->root()->has_children())
    item = mSidebar->root()->child(0);    // (first() is the root, hidden)
  if (item)
    mSidebar->select_only(item, 0);
  mSidebar->redraw();
  showSelection();
}


void DocumentWindow::TreeCB(Fl_Widget *, void * inWindow)
{
  DocumentWindow * window = static_cast<DocumentWindow *>(inWindow);
  Fl_Tree_Reason reason = window->mSidebar->callback_reason();
  if (reason == FL_TREE_REASON_SELECTED || reason == FL_TREE_REASON_DESELECTED)
    window->showSelection();
}


/** The content for what the sidebar has selected: its panel, or a summary
    (what has no panel yet). */
void DocumentWindow::showSelection()
{
  std::string text;
  Fl_Tree_Item * item = mSidebar->first_selected_item();
  const Node * node = item ? static_cast<const Node *>(item->user_data()) : nullptr;
  Fl_Group * panel = nullptr;
  if (!mDeviceFile) {
    mChooserPanel->update();
    panel = mChooserPanel;
  } else if (node) {
    try {
      switch (node->kind) {
        case Node::kDevice:
          mDevicePanel->update(*mDeviceFile);
          panel = mDevicePanel;
          break;
        case Node::kStore:
          for (const StoreRecord & store : mDeviceFile->stores())
            if (store.pk == node->storePk) {
              mStorePanel->update(store);
              panel = mStorePanel;
            }
          break;
        case Node::kSoup:
          mSoupPanel->load(mFile, node->soupPk);
          panel = mSoupPanel;
          break;
        case Node::kScreenshot:
          panel = mScreenshotPanel;
          break;
        case Node::kKeyboard:
          panel = mKeyboardPanel;
          break;
        default:
          break;
      }
    } catch (const ModelError & error) {
      panel = nullptr;
      text = error.what();
    }
  }
  for (Fl_Group * each : { (Fl_Group *)mDevicePanel, (Fl_Group *)mStorePanel, (Fl_Group *)mSoupPanel,
                           (Fl_Group *)mScreenshotPanel, (Fl_Group *)mKeyboardPanel, (Fl_Group *)mChooserPanel })
    if (each != panel)
      each->hide();
  // screenshot mode, keyboard passthrough while their panels show
  if (panel != mScreenshotPanel && mScreenshotPanel->active())
    mScreenshotPanel->leave();
  if (panel != mKeyboardPanel && mKeyboardPanel->active())
    mKeyboardPanel->leave();
  if (panel)
    panel->show();
  if (panel == mScreenshotPanel)
    mScreenshotPanel->enter();
  if (panel == mKeyboardPanel)
    mKeyboardPanel->enter();
  mContent->redraw();
  inspectSelection();
  if (panel) {
    mInfo->hide();
    panel->show();
    return;
  }
  mInfo->show();
  if (node && text.empty()) {      // (text: why the panel couldn't be shown)
    try {
      switch (node->kind) {
        case Node::kDevice:
        case Node::kStore:
        case Node::kSoup:
        case Node::kScreenshot:
        case Node::kKeyboard:
          break;
        case Node::kApp: {
          text = item->label() + std::string("\n\n");
          for (const SoupRecord & soup : mDeviceFile->soups(node->storePk))
            if (soup.appPk == node->appPk)
              text += soup.name + ": " + std::to_string(mDeviceFile->entryCount(soup.pk)) + " entries\n";
          break;
        }
      }
    } catch (const ModelError & error) {
      text = error.what();
    }
  }
  mInfo->copy_label(LabelText(text).c_str());
  mContent->redraw();
}


/** The inspector follows the soup panel's selection (only while it shows:
    reading an entry costs). */
void DocumentWindow::inspectSelection()
{
  if (!mInspectorShown)
    return;
  const Node * node = selectedNode();
  if (!mDeviceFile || !node || node->kind != Node::kSoup)
    mInspector->notApplicable();
  else if (!mSoupPanel->loading()) {
    std::string app;
    try {
      for (const AppRecord & each : mDeviceFile->apps())
        if (each.pk == node->appPk)
          app = each.name;
    } catch (const ModelError &) {
    }
    mInspector->inspect(mFile, mSoupPanel->data().soupName, app, mSoupPanel->selectedEntries());
  }
}


void DocumentWindow::showKeyboard()
{
  if (Fl_Tree_Item * item = mSidebar->find_item("Keyboard")) {
    if (!mKeyboardPanel->visible()) {
      mSidebar->select_only(item, 0);
      showSelection();
    } else
      mKeyboardPanel->enter();
  }
}


bool DocumentWindow::canPaste() const
{
  return mKeyboardPanel->canPaste();
}


void DocumentWindow::paste()
{
  mKeyboardPanel->paste();
}


/*-- the soup panel's entries ---------------------------------------------------*/

const DocumentWindow::Node * DocumentWindow::selectedNode() const
{
  Fl_Tree_Item * item = mSidebar->first_selected_item();
  return item ? static_cast<const Node *>(item->user_data()) : nullptr;
}


bool DocumentWindow::canDeleteEntries() const
{
  const Node * node = selectedNode();
  if (!mDeviceFile || !mWritable || !node || node->kind != Node::kSoup || !mSoupPanel->visible() || mSoupPanel->loading()
      || mSoupPanel->selectedEntries().empty())
    return false;
  App & app = App::get();
  if (app.isDockedWindow(this))
    return !app.dock().busy();
  // packages are removed from the Newton only (a backup doesn't remove them)
  return mSoupPanel->data().soupName != "Packages";
}


void DocumentWindow::deleteEntries()
{
  if (!canDeleteEntries())
    return;
  const Node * node = selectedNode();
  std::vector<int64_t> entries = mSoupPanel->selectedEntries();
  App & app = App::get();
  if (app.isDockedWindow(this)) {
    app.dock().removeEntries(mFile, node->soupPk, entries);
    app.updateMenus();
    return;
  }
  if (!ConfirmUnlessSilenced(pref::kNoDeleteWarning, "Delete", "You are not connected to a Newton device.",
                             "Soup entries will be deleted from your Newton device when you back up next.", "Delete"))
    return;
  std::filesystem::path file = mFile;
  app.objectWork([file, entries]() {
    bool ok = false;
    std::string report = DeleteFromFile(file, entries, ok);
    RunOnMainThread([file, report]() {
      App & app = App::get();
      app.setMessage(report, file);
      app.fileChanged(file);
      app.stateChanged();
    });
  });
}


bool DocumentWindow::canExport() const
{
  const Node * node = selectedNode();
  if (!mDeviceFile || !node)
    return false;
  if (node->kind == Node::kApp)
    return true;
  return node->kind == Node::kSoup && mSoupPanel->visible() && !mSoupPanel->loading() && !mSoupPanel->data().rows.empty();
}


void DocumentWindow::exportEntries()
{
  if (!canExport())
    return;
  Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_DIRECTORY);
  chooser.title("Export into this folder");
  if (chooser.show() == 0 && chooser.filename())
    exportEntriesTo(PathFromUTF8(chooser.filename()));
}


void DocumentWindow::exportEntriesTo(const std::filesystem::path & inFolder)
{
  if (!canExport())
    return;
  const Node * node = selectedNode();
  std::vector<int64_t> entries;
  if (node->kind == Node::kSoup) {
    entries = mSoupPanel->selectedEntries();
    if (entries.empty())          // none selected: all of the soup
      for (const SoupData::Row & row : mSoupPanel->data().rows)
        entries.push_back(row.entryPk);
  }
  std::filesystem::path folder = inFolder, file = mFile;
  int64_t appPk = node->appPk, storePk = node->storePk;
  App::get().setMessage("Exporting...", file);
  updateState();
  App::get().objectWork([file, appPk, storePk, folder, entries]() {
    bool ok = false;
    std::string report = ExportEntries(file, appPk, storePk, folder, entries, ok);
    RunOnMainThread([file, report]() {
      App & app = App::get();
      app.setMessage(report, file);
      app.stateChanged();
    });
  });
}


bool DocumentWindow::canImport() const
{
  return mDeviceFile && mWritable;
}


void DocumentWindow::importFiles(const std::vector<std::filesystem::path> & inFiles)
{
  if (!canImport() || inFiles.empty())
    return;
  // where: the store and app selected, else the internal store
  const Node * node = selectedNode();
  int64_t storePk = targetStore();
  std::string preferred;
  try {
    if (node && (node->kind == Node::kApp || node->kind == Node::kSoup))
      for (const AppRecord & each : mDeviceFile->apps())
        if (each.pk == node->appPk)
          preferred = each.name;
  } catch (const ModelError & error) {
    App::get().setMessage(std::string("Not imported: ") + error.what(), mFile);
    App::get().stateChanged();
    return;
  }
  if (!storePk) {
    App::get().setMessage("Not imported: the backup has no store yet", mFile);
    App::get().stateChanged();
    return;
  }
  // which app takes each file
  std::vector<ImportItem> items;
  std::string refused;
  for (const std::filesystem::path & file : inFiles) {
    std::vector<std::string> apps = ImportAppsForFile(file);
    std::string app;
    if (std::find(apps.begin(), apps.end(), preferred) != apps.end())
      app = preferred;
    else if (apps.size() == 1)
      app = apps[0];
    else if (apps.size() > 1) {
      int choice = ChooseOne("Import", "Which app takes " + UTF8FromPath(file.filename()) + "?", apps);
      if (choice < 0)
        continue;
      app = apps[choice];
    }
    if (app.empty())
      refused += (refused.empty() ? "" : ", ") + UTF8FromPath(file.filename());
    else
      items.push_back({ file, app });
  }
  App & app = App::get();
  if (items.empty()) {
    app.setMessage(refused.empty() ? std::string("Nothing imported") : "No app imports " + refused, mFile);
    app.stateChanged();
    return;
  }
  if (app.isDockedWindow(this)) {
    std::vector<std::pair<std::filesystem::path, std::string>> pairs;
    for (const ImportItem & item : items)
      pairs.push_back({ item.source, item.app });
    app.dock().importFiles(mFile, storePk, pairs);
    app.updateMenus();
    return;
  }
  if (!ConfirmUnlessSilenced(pref::kNoAddWarning, "Import", "You are not connected to a Newton device.",
                             "Soup entries will be added to your Newton device when you sync next.", "OK"))
    return;
  std::filesystem::path file = mFile;
  app.objectWork([file, storePk, items]() {
    std::vector<int64_t> entries, soups;
    bool ok = false;
    std::string report = ImportIntoFile(file, storePk, items, entries, soups, ok);
    RunOnMainThread([file, report]() {
      App & app = App::get();
      app.setMessage(report, file);
      app.fileChanged(file);
      app.stateChanged();
    });
  });
}


int64_t DocumentWindow::targetStore() const
{
  const Node * node = selectedNode();
  if (node && (node->kind == Node::kStore || node->kind == Node::kApp || node->kind == Node::kSoup))
    return node->storePk;
  try {
    std::vector<StoreRecord> stores = mDeviceFile->stores();
    SortStores(stores);
    return stores.empty() ? 0 : stores[0].pk;
  } catch (const ModelError &) {
    return 0;
  }
}


void DocumentWindow::copyEntriesHere(const std::filesystem::path & inSource, const std::vector<int64_t> & inEntryPks)
{
  App & app = App::get();
  int64_t storePk = canImport() ? targetStore() : 0;
  if (inSource == mFile || inEntryPks.empty() || !storePk)
    return;
  if (app.isDockedWindow(this)) {
    if (app.dock().busy()) {
      app.setMessage("Not copied: the Newton is busy", mFile);
      app.stateChanged();
      return;
    }
    app.dock().copyEntries(inSource, inEntryPks, mFile, storePk);
    app.updateMenus();
    return;
  }
  if (!ConfirmUnlessSilenced(pref::kNoAddWarning, "Copy", "You are not connected to a Newton device.",
                             "Soup entries will be added to your Newton device when you sync next.", "OK"))
    return;
  std::filesystem::path file = mFile;
  app.objectWork([inSource, inEntryPks, file, storePk]() {
    std::vector<int64_t> entries, soups;
    bool ok = false;
    std::string report = CopyEntries(inSource, inEntryPks, file, storePk, entries, soups, ok);
    RunOnMainThread([file, report]() {
      App & app = App::get();
      app.setMessage(report, file);
      app.fileChanged(file);
      app.stateChanged();
    });
  });
}


void DocumentWindow::dropped(const std::string & inText)
{
  std::filesystem::path source;
  std::vector<int64_t> entries;
  if (ParseEntriesPayload(inText, source, entries)) {
    copyEntriesHere(source, entries);
    return;
  }
  std::vector<std::filesystem::path> packages, others;
  for (const std::filesystem::path & file : DroppedFiles(inText))
    (IsNewtonPackage(file) ? packages : others).push_back(file);
  App & app = App::get();
  if (!packages.empty()) {
    if (app.isDockedWindow(this))
      app.installPackages(packages);
    else
      others.insert(others.end(), packages.begin(), packages.end());   // the importers may take them
  }
  if (!others.empty())
    importFiles(others);
}


int DocumentWindow::handle(int inEvent)
{
  switch (inEvent) {
    case FL_DND_ENTER:
    case FL_DND_DRAG:
      if (Fl_Double_Window::handle(inEvent)) {
        mDropping = false;  // a panel takes it
        return 1;
      }
      mDropping = mDeviceFile != nullptr;
      return mDropping ? 1 : 0;
    case FL_DND_LEAVE:
      mDropping = false;
      return Fl_Double_Window::handle(inEvent);
    case FL_DND_RELEASE:
      if (mDropping)
        return 1;
      return Fl_Double_Window::handle(inEvent);
    case FL_PASTE:
      if (mDropping) {
        mDropping = false;
        dropped(std::string(Fl::event_text() ? Fl::event_text() : "", Fl::event_length()));
        return 1;
      }
      break;
  }
  return Fl_Double_Window::handle(inEvent);
}


bool DocumentWindow::canSelectAll() const
{
  return mSoupPanel->visible() && !mSoupPanel->loading() && !mSoupPanel->data().rows.empty();
}


void DocumentWindow::selectAll()
{
  if (canSelectAll())
    mSoupPanel->selectAll();
}


/*-- panes --------------------------------------------------------------------*/

void DocumentWindow::layoutTile()
{
  int x = mTile->x(), y = mTile->y(), w = mTile->w(), h = mTile->h();
  // a window waiting for a Newton has nothing in its sidebar
  int sidebar = mSidebarShown && mDeviceFile ? mSidebarWidth : 0;
  int inspector = mInspectorShown ? mInspectorWidth : 0;
  // the content keeps some room
  int over = sidebar + inspector + kMinContentWidth - w;
  if (over > 0 && inspector > 0) {
    int cut = std::min(over, inspector - 120);
    inspector -= std::max(0, cut);
    over -= std::max(0, cut);
  }
  if (over > 0 && sidebar > 0)
    sidebar -= std::min(over, sidebar - 120);
  mSidebar->resize(x, y, sidebar, h);
  mContent->resize(x + sidebar, y, w - sidebar - inspector, h);
  mInspector->resize(x + w - inspector, y, inspector, h);
  if (sidebar > 0) mSidebar->show(); else mSidebar->hide();
  if (inspector > 0) mInspector->show(); else mInspector->hide();
  mTile->init_sizes();
  mSidebarButton->value(mSidebarShown);
  mInspectorButton->value(mInspectorShown);
  mTile->redraw();
}


void DocumentWindow::showSidebar(bool inShow)
{
  if (mSidebarShown && mSidebar->visible())
    mSidebarWidth = mSidebar->w();
  mSidebarShown = inShow;
  layoutTile();
}


void DocumentWindow::showInspector(bool inShow)
{
  if (mInspectorShown && mInspector->visible())
    mInspectorWidth = mInspector->w();
  mInspectorShown = inShow;
  layoutTile();
  inspectSelection();
}


void DocumentWindow::rememberLayout()
{
  Prefs & prefs = Prefs::App();
  prefs.setInt(kPrefWidth, w());
  prefs.setInt(kPrefHeight, h());
  if (mSidebarShown && mSidebar->visible())
    prefs.setInt(kPrefSidebarWidth, mSidebar->w());
  if (mInspectorShown && mInspector->visible())
    prefs.setInt(kPrefInspectorWidth, mInspector->w());
  prefs.setBool(kPrefSidebarShown, mSidebarShown);
  prefs.setBool(kPrefInspectorShown, mInspectorShown);
}


void DocumentWindow::ToggleCB(Fl_Widget * inButton, void * inCommand)
{
  DocumentWindow * window = static_cast<DocumentWindow *>(inButton->window());
  App::get().command((Command)(intptr_t)inCommand, window);
}


/*-- the dock's state ---------------------------------------------------------*/

void DocumentWindow::updateState()
{
  App & app = App::get();
  if (mDeviceFile && (app.isDockedWindow(this) && !app.dock().newton1()) != mShowsDockItems)
    fillTree();
  bool mine = mFile.empty() || app.isDockedWindow(this);
  bool message = app.messageFile().empty() ? mine : app.messageFile() == mFile;
  mMessage->copy_label(LabelText(message ? app.message() : std::string()).c_str());
  std::string state = app.dockStatus();
  if (app.docked() && !mFile.empty() && !app.isDockedWindow(this))
    state = app.dockedName() + " is docked (another window)";
  mDockState->copy_label(LabelText(state).c_str());
  if (!mTimerRunning && app.dock().busy() && mine) {
    mTimerRunning = true;
    Fl::add_timeout(0.25, ProgressTimer, this);
  }
  if (mDevicePanel->visible())
    mDevicePanel->updateState();
  if (mScreenshotPanel->visible())
    mScreenshotPanel->updateState();
  updateProgress();
  redraw();
}


void DocumentWindow::updateProgress()
{
  App & app = App::get();
  std::shared_ptr<Progress> progress = app.isDockedWindow(this) ? app.dock().progress() : nullptr;
  if (!progress) {
    mActivity->label(nullptr);
    mProgress->hide();
    mCancel->hide();
    return;
  }
  mActivity->copy_label(LabelText(progress->description()).c_str());
  double fraction = progress->fraction();
  mProgress->minimum(0);
  mProgress->maximum(1);
  mProgress->value(fraction < 0 ? 0 : (float)fraction);
  mProgress->show();
  mCancel->show();
  if (progress->cancelled())
    mCancel->deactivate();
  else
    mCancel->activate();
  mToolbar->redraw();
}


void DocumentWindow::ProgressTimer(void * inWindow)
{
  DocumentWindow * window = static_cast<DocumentWindow *>(inWindow);
  window->updateProgress();
  if (App::get().dock().busy())
    Fl::repeat_timeout(0.25, ProgressTimer, inWindow);
  else
    window->mTimerRunning = false;
}


void DocumentWindow::CancelCB(Fl_Widget *, void *)
{
  App::get().dock().cancel();
}

} // namespace ncx
