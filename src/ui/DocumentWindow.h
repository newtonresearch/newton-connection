/*
 File: DocumentWindow.h

 A document window (PORTING.md 7.2; the Cocoa app's MainWindow): one
 .newtondevice file, or none yet ("Waiting for a Newton").
   - off macOS, the menu bar (D9: a full menu in every document window)
   - the toolbar: the sidebar toggle, what the dock does (its progress,
     Cancel), the inspector toggle
   - an Fl_Tile: the sidebar (an Fl_Tree: the device, Keyboard and
     Screenshot while the Newton is docked, then the stores, their apps and
     their soups), the content (what the sidebar has selected), the
     inspector
   - the status line: what happened last, and the dock's state
 It reads its file itself (read only: the dock thread writes it, SQLite
 keeps them apart) and is told to reload it after a backup. No Newton
 objects (Refs) here: they are the dock thread's.
 */

#ifndef NCX_UI_DOCUMENTWINDOW_H
#define NCX_UI_DOCUMENTWINDOW_H

#include <FL/Fl_Double_Window.H>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class Fl_Box;
class Fl_Button;
class Fl_Group;
class Fl_Menu_;
class Fl_Progress;
class Fl_Tile;
class Fl_Tree;

namespace ncx {

class DeviceFile;
class Inspector;
class DevicePanel;
class StorePanel;
class SoupPanel;

class DocumentWindow : public Fl_Double_Window
{
public:
  /** One without a file (Waiting for a Newton). */
  DocumentWindow();
  ~DocumentWindow() override;

  /** Show this file (a window has one file, once). False and why if it
      can't be opened. */
  bool bind(const std::filesystem::path & inFile, std::string & outError);
  /** Its file ("" none). */
  const std::filesystem::path & file() const { return mFile; }
  /** Read the file again (a backup changed it). */
  void reload();
  /** Show what the app and the dock do now. */
  void updateState();

  bool sidebarShown() const { return mSidebarShown; }
  void showSidebar(bool inShow);
  bool inspectorShown() const { return mInspectorShown; }
  void showInspector(bool inShow);

  /** The soup panel's entries: delete those selected (on the Newton too
      when it is docked; packages only then), export those selected (or
      all of the soup, or of the app selected in the sidebar), select all
      of them. */
  bool canDeleteEntries() const;
  void deleteEntries();
  bool canExport() const;
  void exportEntries();
  void exportEntriesTo(const std::filesystem::path & inFolder);
  bool canSelectAll() const;
  void selectAll();

  /** Its menu bar (nullptr on macOS: the app has one). */
  Fl_Menu_ * menuBar() { return mMenuBar; }

  /** The sidebar, the status line's message, the panels (tests). */
  Fl_Tree * sidebar() { return mSidebar; }
  DevicePanel * devicePanel() { return mDevicePanel; }
  StorePanel * storePanel() { return mStorePanel; }
  SoupPanel * soupPanel() { return mSoupPanel; }
  Inspector * inspector() { return mInspector; }
  const char * messageText() const;

  /** Closed: the app forgets it. */
  void hide() override;

private:
  struct Node;
  const Node * selectedNode() const;
  void fillTree();
  void showSelection();
  void inspectSelection();
  void layoutTile();
  void rememberLayout();
  void updateProgress();
  static void TreeCB(Fl_Widget *, void *);
  static void ToggleCB(Fl_Widget *, void *);
  static void CancelCB(Fl_Widget *, void *);
  static void ProgressTimer(void *);

  std::unique_ptr<DeviceFile> mDeviceFile;
  std::filesystem::path mFile;
  std::string mTitle;
  std::vector<std::unique_ptr<Node>> mNodes;
  bool mShowsDockItems = false;
  bool mSidebarShown = true;
  bool mInspectorShown = false;
  int mSidebarWidth = 220;
  int mInspectorWidth = 260;
  bool mTimerRunning = false;

  Fl_Menu_ * mMenuBar = nullptr;
  Fl_Group * mToolbar = nullptr;
  Fl_Button * mSidebarButton = nullptr;
  Fl_Button * mInspectorButton = nullptr;
  Fl_Box * mActivity = nullptr;
  Fl_Progress * mProgress = nullptr;
  Fl_Button * mCancel = nullptr;
  Fl_Tile * mTile = nullptr;
  Fl_Tree * mSidebar = nullptr;
  Fl_Group * mContent = nullptr;
  Fl_Box * mInfo = nullptr;
  DevicePanel * mDevicePanel = nullptr;
  StorePanel * mStorePanel = nullptr;
  SoupPanel * mSoupPanel = nullptr;
  Inspector * mInspector = nullptr;
  Fl_Box * mMessage = nullptr;
  Fl_Box * mDockState = nullptr;
};

} // namespace ncx

#endif // NCX_UI_DOCUMENTWINDOW_H
