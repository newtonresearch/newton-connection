/*
 File: ChooserPanel.cc

 The device chooser (ChooserPanel.h).
 */

#include "ui/panels/ChooserPanel.h"
#include "ui/App.h"

#include "base/Dates.h"
#include "platform/AppFolders.h"

#include <FL/Fl.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>

#include <chrono>

namespace ncx {

namespace {

/** A file's time as seconds since 1970. */
int64_t ModifiedTime(const std::filesystem::path & inFile)
{
  std::error_code error;
  auto time = std::filesystem::last_write_time(inFile, error);
  if (error)
    return 0;
  // file_clock to system_clock, the C++17 way
  auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
    time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
  return std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
}

/** Browser text: '@' starts a format there. */
std::string BrowserText(const std::string & inText)
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


ChooserPanel::ChooserPanel(int inX, int inY, int inW, int inH)
  : ChooserPanelUI(inX, inY, 560, 400)      // as designed, then to the size asked
{
  box(FL_NO_BOX);
  resize(inX, inY, inW, inH);
  static const int widths[] = { 220, 170, 0 };
  mBackups->column_widths(widths);
  mBackups->column_char('\t');
  mBackups->callback(BrowserCB, this);
  mBackups->when(FL_WHEN_CHANGED | FL_WHEN_NOT_CHANGED);
  mOpen->callback(OpenCB, this);
  mOther->callback(OtherCB, this);
  update();
}


void ChooserPanel::update()
{
  mBackups->clear();
  mFiles = ListDeviceFiles(AppDataFolder());
  for (const DeviceFileInfo & backup : mFiles) {
    std::string line = BrowserText(backup.newtonName.empty() ? UTF8FromPath(backup.path.stem()) : backup.newtonName)
                     + "\t" + BrowserText(backup.newtonId) + "\t" + FormatRelativeDateTime(ModifiedTime(backup.path), Now());
    mBackups->add(line.c_str());
  }
  if (!mFiles.empty())
    mBackups->select(1);
  if (mFiles.empty())
    mOpen->deactivate();
  else
    mOpen->activate();
  redraw();
}


void ChooserPanel::OpenCB(Fl_Widget *, void * inPanel)
{
  ChooserPanel * panel = static_cast<ChooserPanel *>(inPanel);
  int line = panel->mBackups->value();
  if (line >= 1 && line <= (int)panel->mFiles.size())
    App::get().openDocument(panel->mFiles[line - 1].path);
}


void ChooserPanel::OtherCB(Fl_Widget *, void *)
{
  App::get().command(Command::kOpen, nullptr);
}


void ChooserPanel::BrowserCB(Fl_Widget * inBrowser, void * inPanel)
{
  if (Fl::event_clicks() && Fl::event() == FL_RELEASE)
    OpenCB(inBrowser, inPanel);
}

} // namespace ncx
