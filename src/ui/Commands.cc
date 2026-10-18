/*
 File: Commands.cc

 NCX's menu (Commands.h).
 */

#include "ui/Commands.h"
#include "ui/App.h"
#include "ui/DocumentWindow.h"

#include <FL/Fl.H>
#include <FL/Fl_Menu_.H>
#include <FL/Fl_Sys_Menu_Bar.H>
#include <FL/Fl_Window.H>

#include <cstdint>

namespace ncx {

namespace {

struct Item
{
  const char * path;            // "&File/&Open..."
  int shortcut;
  Command command;
  int flags;                    // FL_MENU_DIVIDER after it, FL_MENU_RADIO
};

// (on macOS the application menu has About, Preferences and Quit: Fl_Mac_App_Menu)
const Item kItems[] = {
  { "&File/&New Connection", FL_COMMAND + 'n', Command::kNewConnection, 0 },
  { "&File/&Open" "\xE2\x80\xA6", FL_COMMAND + 'o', Command::kOpen, FL_MENU_DIVIDER },
  { "&File/&Import" "\xE2\x80\xA6", FL_COMMAND + FL_SHIFT + 'i', Command::kImport, 0 },
  { "&File/&Export" "\xE2\x80\xA6", FL_COMMAND + 'e', Command::kExport, FL_MENU_DIVIDER },
  { "&File/&Close", FL_COMMAND + 'w', Command::kClose, 0 },
#if !defined(__APPLE__)
  { "&File/&Preferences" "\xE2\x80\xA6", FL_COMMAND + ',', Command::kPreferences, FL_MENU_DIVIDER },
  { "&File/&Quit", FL_COMMAND + 'q', Command::kQuit, 0 },
#endif
  { "&Edit/&Copy", FL_COMMAND + 'c', Command::kCopy, 0 },
  { "&Edit/&Paste", FL_COMMAND + 'v', Command::kPaste, 0 },
#if defined(__APPLE__)
  { "&Edit/&Delete", FL_BackSpace, Command::kDelete, 0 },
#else
  { "&Edit/&Delete", FL_Delete, Command::kDelete, 0 },
#endif
  { "&Edit/Select &All", FL_COMMAND + 'a', Command::kSelectAll, 0 },
  { "&Newton/&Back Up", FL_COMMAND + 'b', Command::kBackUp, 0 },
  { "&Newton/&Restore" "\xE2\x80\xA6", 0, Command::kRestore, FL_MENU_DIVIDER },
  { "&Newton/&Install Package" "\xE2\x80\xA6", FL_COMMAND + 'i', Command::kInstallPackage, FL_MENU_DIVIDER },
  { "&Newton/&Screenshot", FL_COMMAND + FL_SHIFT + 's', Command::kScreenshot, 0 },
  { "&Newton/&Keyboard", FL_COMMAND + 'k', Command::kKeyboard, FL_MENU_DIVIDER },
  { "&Newton/Dump &ROM" "\xE2\x80\xA6", 0, Command::kDumpROM, 0 },
  { "&Newton/Dump &Card" "\xE2\x80\xA6", 0, Command::kDumpCard, FL_MENU_DIVIDER },
  { "&Newton/NewtonOS &1 Session/&Sync", 0, Command::kNewton1Sync, FL_MENU_RADIO },
  { "&Newton/NewtonOS &1 Session/&Restore", 0, Command::kNewton1Restore, FL_MENU_RADIO },
  { "&Newton/NewtonOS &1 Session/&Install Package", 0, Command::kNewton1Install, FL_MENU_RADIO | FL_MENU_DIVIDER },
  { "&Newton/&Disconnect", 0, Command::kDisconnect, 0 },
  { "&View/&Sidebar", FL_COMMAND + FL_ALT + 's', Command::kToggleSidebar, 0 },
  { "&View/&Inspector", FL_COMMAND + FL_ALT + 'i', Command::kToggleInspector, 0 },
  { "&Help/Newton Connection &Help", FL_COMMAND + '?', Command::kHelp, 0 },
  { "&Help/&Report a Bug" "\xE2\x80\xA6", 0, Command::kReportBug, 0 },
#if !defined(__APPLE__)
  { "&Help/&About Newton Connection", 0, Command::kAbout, 0 },
#endif
};

/** The window a menu item came from: the document window holding its menu
    bar (Windows, Linux), else the topmost one. */
DocumentWindow * TargetOf(Fl_Widget * inWidget)
{
  if (inWidget)
    if (DocumentWindow * window = dynamic_cast<DocumentWindow *>(inWidget->window()))
      return window;
  return App::get().frontWindow();
}

void MenuCB(Fl_Widget * inWidget, void * inCommand)
{
  App::get().command((Command)(intptr_t)inCommand, TargetOf(inWidget));
}

} // namespace


void BuildMenu(Fl_Menu_ & ioMenu)
{
  for (const Item & item : kItems)
    ioMenu.add(item.path, item.shortcut, MenuCB, (void *)(intptr_t)item.command, item.flags);
}


void UpdateMenu(Fl_Menu_ & ioMenu)
{
  DocumentWindow * target = TargetOf(dynamic_cast<Fl_Sys_Menu_Bar *>(&ioMenu) ? nullptr : &ioMenu);
  App & app = App::get();
  for (int i = 0; i < ioMenu.size(); ++i) {
    Fl_Menu_Item * item = const_cast<Fl_Menu_Item *>(&ioMenu.menu()[i]);
    if (!item->label() || item->callback() != MenuCB)
      continue;
    Command command = (Command)(intptr_t)item->user_data();
    if (app.canDo(command, target))
      item->activate();
    else
      item->deactivate();
    if (item->flags & FL_MENU_RADIO) {
      if (app.isChecked(command, target))
        item->setonly(const_cast<Fl_Menu_Item *>(ioMenu.menu()));
    }
  }
  if (Fl_Sys_Menu_Bar * bar = dynamic_cast<Fl_Sys_Menu_Bar *>(&ioMenu))
    bar->update();
}

} // namespace ncx
