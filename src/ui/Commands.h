/*
 File: Commands.h

 NCX's menu (PORTING.md 7.3, D9): one table of commands, made into the
 system menu bar on macOS (one for the app) and into a menu bar in every
 document window elsewhere. A command goes to the window whose menu bar
 it came from, or (macOS, and shortcuts) to the topmost document window;
 the app takes those no window needs (About, Preferences, Quit, ...).

 The Cocoa app's Main.storyboard had: the application menu, File (New
 Connection, Open, Open Recent, Install Package, Dump Newton ROM, Close),
 Edit (Copy, Paste, Delete, Select All, Find), Newton 1 (the session to
 start: Sync, Restore, Install Package), Window, Help. The Newton menu here
 gathers what the Cocoa app had on its panels' buttons.
 */

#ifndef NCX_UI_COMMANDS_H
#define NCX_UI_COMMANDS_H

class Fl_Menu_;

namespace ncx {

enum class Command
{
  kNone,
  // the app
  kAbout, kPreferences, kQuit, kHelp, kReportBug,
  // File
  kNewConnection, kOpen, kClose, kExport, kImport,
  // Edit
  kCopy, kPaste, kDelete, kSelectAll,
  // Newton
  kBackUp, kRestore, kInstallPackage, kScreenshot, kKeyboard, kDumpROM, kDumpCard, kDisconnect,
  kNewton1Sync, kNewton1Restore, kNewton1Install,
  // Window
  kToggleSidebar, kToggleInspector,
};

/** The menu's items into a menu bar (Fl_Sys_Menu_Bar on macOS, an
    Fl_Menu_Bar in a window elsewhere). */
void BuildMenu(Fl_Menu_ & ioMenu);

/** Each item enabled as the app says (App::canDo), and the Newton 1
    session ticked. */
void UpdateMenu(Fl_Menu_ & ioMenu);

} // namespace ncx

#endif // NCX_UI_COMMANDS_H
