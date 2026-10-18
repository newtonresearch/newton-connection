/*
 File: DocumentWindow.cc

 A document window (DocumentWindow.h).
 */

#include "ui/DocumentWindow.h"
#include "ui/App.h"
#include "ui/Commands.h"

#include "base/Dates.h"
#include "base/Prefs.h"
#include "base/Version.h"
#include "model/DeviceFile.h"
#include "platform/AppFolders.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Tile.H>
#include <FL/Fl_Toggle_Button.H>
#include <FL/Fl_Tree.H>

#include <algorithm>
#include <cstdint>

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

std::string Kilobytes(int64_t inBytes)
{
  return std::to_string((inBytes + 512) / 1024) + " KB";
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
  mContent->resizable(mInfo);
  mContent->end();
  mInspectorText = new Fl_Text_Buffer();
  mInspector = new Fl_Text_Display(w() - mInspectorWidth, top, mInspectorWidth, tileHeight);
  mInspector->buffer(mInspectorText);
  mInspector->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
  mInspectorText->text("Select an entry to see it here.");
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
  mInspector->buffer(nullptr);
  delete mInspectorText;
}


bool DocumentWindow::bind(const std::filesystem::path & inFile, std::string & outError)
{
  std::unique_ptr<DeviceFile> file = DeviceFile::Open(inFile, DeviceFile::kReadOnly, outError);
  if (!file)
    return false;
  mDeviceFile = std::move(file);
  mFile = inFile;
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
  auto add = [this](Fl_Tree_Item * inParent, const std::string & inLabel, Node inNode) {
    Fl_Tree_Item * item = inParent ? mSidebar->add(inParent, inLabel.c_str()) : mSidebar->add(mSidebar->root(), inLabel.c_str());
    mNodes.emplace_back(new Node(inNode));
    item->user_data(mNodes.back().get());
    return item;
  };
  if (mDeviceFile) {
    std::string name = mDeviceFile->newtonName();
    add(nullptr, name.empty() ? std::string("Newton") : name, Node { Node::kDevice });
    if (mShowsDockItems) {
      add(nullptr, "Keyboard", Node { Node::kKeyboard });
      add(nullptr, "Screenshot", Node { Node::kScreenshot });
    }
    try {
      std::vector<StoreRecord> stores = mDeviceFile->stores();
      SortStores(stores);
      std::vector<SoupRecord> soups = mDeviceFile->soups();
      for (const StoreRecord & store : stores) {
        Fl_Tree_Item * storeItem = add(nullptr, store.name, Node { Node::kStore, store.pk });
        for (const AppRecord & appRecord : mDeviceFile->storeApps(store.pk, true)) {
          Fl_Tree_Item * appItem = add(storeItem, appRecord.name, Node { Node::kApp, store.pk, appRecord.pk });
          for (const SoupRecord & soup : soups)
            if (soup.storePk == store.pk && soup.appPk == appRecord.pk)
              add(appItem, soup.name, Node { Node::kSoup, store.pk, appRecord.pk, soup.pk });
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


/** The content for what the sidebar has selected. For now a summary; the
    panels come with PORTING.md 7.4. */
void DocumentWindow::showSelection()
{
  std::string text;
  Fl_Tree_Item * item = mSidebar->first_selected_item();
  const Node * node = item ? static_cast<const Node *>(item->user_data()) : nullptr;
  if (!mDeviceFile) {
    text = "Waiting for a Newton.\n\nOn the Newton, open Dock (Connection on NewtonOS 1) and connect "
           "with TCP/IP or serial. Or open a backup with File > Open.";
  } else if (!node) {
    text = "";
  } else {
    try {
      switch (node->kind) {
        case Node::kDevice: {
          text = mDeviceFile->newtonName() + "\n\n";
          if (std::optional<DeviceRecord> device = mDeviceFile->device()) {
            NewtonInfo info = NewtonInfoOf(*device);
            text += MachineTypeName(info) + " (" + ManufacturerName(info) + ")\n";
            if (device->osInfo)
              text += "NewtonOS " + *device->osInfo + "\n";
            if (device->processor)
              text += *device->processor + "\n";
            text += "Serial number " + SerialNumberText(info) + "\n";
            text += "Last backup: "
                  + (device->backupDate ? FormatDateTime(TimeFromCoreData(*device->backupDate)) : std::string("never"))
                  + "\n";
          }
          text += "\n" + UTF8FromPath(mFile);
          break;
        }
        case Node::kKeyboard:
          text = "Type on the Newton from this computer's keyboard (comes with PORTING.md 7.4).";
          break;
        case Node::kScreenshot:
          text = "The Newton's screen (comes with PORTING.md 7.4).";
          break;
        case Node::kStore:
          for (const StoreRecord & store : mDeviceFile->stores())
            if (store.pk == node->storePk) {
              text = store.name + "\n\n" + store.kind + (store.defaultStore ? ", the default store" : "")
                   + (store.readOnly ? ", read only" : "") + "\n"
                   + Kilobytes(store.usedSize) + " used of " + Kilobytes(store.totalSize) + "\n";
            }
          break;
        case Node::kApp: {
          text = item->label() + std::string("\n\n");
          for (const SoupRecord & soup : mDeviceFile->soups(node->storePk))
            if (soup.appPk == node->appPk)
              text += soup.name + ": " + std::to_string(mDeviceFile->entryCount(soup.pk)) + " entries\n";
          break;
        }
        case Node::kSoup:
          if (std::optional<SoupRecord> soup = mDeviceFile->soup(node->soupPk)) {
            text = soup->name + "\n\n";
            if (!soup->descr.empty())
              text += soup->descr + "\n";
            text += std::to_string(mDeviceFile->entryCount(soup->pk)) + " entries\n";
            if (soup->lastBackupTime)
              text += "Backed up " + FormatDateTime(TimeFromNewton(soup->lastBackupTime)) + "\n";
          }
          break;
      }
    } catch (const ModelError & error) {
      text = error.what();
    }
  }
  mInfo->copy_label(LabelText(text).c_str());
  mContent->redraw();
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
  mMessage->copy_label(LabelText(mine ? app.message() : std::string()).c_str());
  std::string state = app.dockStatus();
  if (app.docked() && !mFile.empty() && !app.isDockedWindow(this))
    state = app.dockedName() + " is docked (another window)";
  mDockState->copy_label(LabelText(state).c_str());
  if (!mTimerRunning && app.dock().busy() && mine) {
    mTimerRunning = true;
    Fl::add_timeout(0.25, ProgressTimer, this);
  }
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
