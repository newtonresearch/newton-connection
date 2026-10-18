/*
 File: ScreenshotPanel.cc

 The screenshot panel (ScreenshotPanel.h).
 */

#include "ui/panels/ScreenshotPanel.h"
#include "ui/App.h"
#include "ui/DocumentWindow.h"

#include "comms/DockProtocol.h"
#include "platform/AppFolders.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Copy_Surface.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Scroll.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

namespace ncx {

ScreenshotPanel::ScreenshotPanel(int inX, int inY, int inW, int inH)
  : ScreenshotPanelUI(inX, inY, 560, 400)      // as designed, then to the size asked
{
  box(FL_NO_BOX);
  resize(inX, inY, inW, inH);
  mTake->callback(TakeCB, this);
  mSave->callback(SaveCB, this);
  mCopy->callback(CopyCB, this);
  mImage->box(FL_NO_BOX);
  updateState();
}


ScreenshotPanel::~ScreenshotPanel()
{
  mImage->image(nullptr);
}


void ScreenshotPanel::setInstructions(const std::string & inText)
{
  mInstructions->copy_label(inText.c_str());
  redraw();
}


std::string ScreenshotPanel::instructions() const
{
  return mInstructions->label() ? mInstructions->label() : "";
}


void ScreenshotPanel::enter()
{
  App & app = App::get();
  DocumentWindow * target = dynamic_cast<DocumentWindow *>(window());
  if (!app.isDockedWindow(target) || app.dock().newton1()) {
    setInstructions(mShot.width ? "Not connected. The last screenshot:" : "Not connected.");
    mActive = mReady = false;
    updateState();
    return;
  }
  if (!mActive) {
    mActive = true;
    mReady = false;
    setInstructions("Please wait...");
    app.dock().beginScreenshots();
  }
  updateState();
}


void ScreenshotPanel::leave()
{
  if (mActive && App::get().docked())
    App::get().dock().endScreenshots();
  mActive = mReady = mTaking = false;
  updateState();
}


void ScreenshotPanel::ready(int inError)
{
  if (!mActive)
    return;
  mReady = inError == 0;
  if (inError == kDockErrFileNotFound)
    setInstructions("Your Newton device needs the Toolkit app for screenshots: install Toolkit.newtonpkg "
                    "(it comes with NTK) and try again.");
  else if (inError != 0)
    setInstructions("No screenshots now (error " + std::to_string(inError) + ").");
  else
    setInstructions("Compose the screen on your Newton device, then take the screenshot.");
  updateState();
}


void ScreenshotPanel::taken(const Screenshot & inShot, int inError)
{
  mTaking = false;
  if (inError != 0 || inShot.width <= 0) {
    setInstructions("No screenshot (error " + std::to_string(inError) + ").");
    updateState();
    return;
  }
  mShot = inShot;
  mRGB.resize((size_t)mShot.width * mShot.height * 3);
  for (size_t i = 0; i < mShot.gray.size() && 3 * i + 2 < mRGB.size(); ++i)
    mRGB[3 * i] = mRGB[3 * i + 1] = mRGB[3 * i + 2] = mShot.gray[i];
  mImage->image(nullptr);
  mImageData.reset(new Fl_RGB_Image(mRGB.data(), mShot.width, mShot.height, 3));
  mImage->resize(mScroll->x() + 2, mScroll->y() + 2, mShot.width, mShot.height);
  mImage->image(mImageData.get());
  mScroll->scroll_to(0, 0);
  if (mActive)
    setInstructions("Compose the screen on your Newton device, then take the screenshot.");
  updateState();
  mScroll->redraw();
}


void ScreenshotPanel::updateState()
{
  App & app = App::get();
  bool idle = !app.dock().busy();
  if (mActive && mReady && idle && !mTaking)
    mTake->activate();
  else
    mTake->deactivate();
  if (mShot.width > 0) {
    mSave->activate();
    mCopy->activate();
  } else {
    mSave->deactivate();
    mCopy->deactivate();
  }
}


bool ScreenshotPanel::save(const std::filesystem::path & inFile) const
{
  if (mShot.width <= 0)
    return false;
  return fl_write_png(UTF8FromPath(inFile).c_str(), mRGB.data(), mShot.width, mShot.height, 3) == 0;
}


void ScreenshotPanel::TakeCB(Fl_Widget *, void * inPanel)
{
  ScreenshotPanel * panel = static_cast<ScreenshotPanel *>(inPanel);
  if (!panel->mActive || !panel->mReady)
    return;
  panel->mTaking = true;
  panel->setInstructions("Receiving the screen...");
  App::get().dock().takeScreenshot();
  panel->updateState();
}


void ScreenshotPanel::SaveCB(Fl_Widget *, void * inPanel)
{
  ScreenshotPanel * panel = static_cast<ScreenshotPanel *>(inPanel);
  Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
  chooser.title("Save the screenshot");
  chooser.filter("PNG image\t*.png");
  chooser.preset_file("Screenshot.png");
  chooser.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM | Fl_Native_File_Chooser::USE_FILTER_EXT);
  if (chooser.show() != 0 || !chooser.filename())
    return;
  std::filesystem::path file = PathFromUTF8(chooser.filename());
  if (file.extension().empty())
    file += ".png";
  if (!panel->save(file))
    fl_alert("Couldn't save %s", UTF8FromPath(file).c_str());
}


void ScreenshotPanel::CopyCB(Fl_Widget *, void * inPanel)
{
  ScreenshotPanel * panel = static_cast<ScreenshotPanel *>(inPanel);
  if (panel->mShot.width <= 0)
    return;
  // drawn on a copy surface, which goes to the clipboard when it goes
  Fl_Copy_Surface surface(panel->mShot.width, panel->mShot.height);
  Fl_Surface_Device::push_current(&surface);
  fl_color(FL_WHITE);
  fl_rectf(0, 0, panel->mShot.width, panel->mShot.height);
  panel->mImageData->draw(0, 0);
  Fl_Surface_Device::pop_current();
}

} // namespace ncx
