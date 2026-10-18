/*
 File: ScreenshotPanel.h

 The screenshot panel (PORTING.md 7.4; the Cocoa app's
 NCScreenshotViewController): while it shows and the Newton is docked, the
 Newton is in screenshot mode (the Dock slip away: AppDock's screenshots);
 Take Screenshot takes one, Save writes it as PNG, Copy puts it on the
 clipboard. Not docked: the last shot of this window, if any. Its layout:
 ScreenshotPanelUI.fl.
 */

#ifndef NCX_UI_PANELS_SCREENSHOTPANEL_H
#define NCX_UI_PANELS_SCREENSHOTPANEL_H

#include "ScreenshotPanelUI.h"

#include "dock/Screenshot.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class Fl_RGB_Image;

namespace ncx {

class ScreenshotPanel : public ScreenshotPanelUI
{
public:
  ScreenshotPanel(int inX, int inY, int inW, int inH);
  ~ScreenshotPanel() override;

  /** Shown: screenshot mode while docked. */
  void enter();
  /** Hidden (another panel, undocked, the window closes): mode ends. */
  void leave();

  /** From the dock (on the UI thread): ready, or why not; a shot. */
  void ready(int inError);
  void taken(const Screenshot & inShot, int inError);
  /** The buttons as the app allows them now. */
  void updateState();

  /** The last shot (tests), and saving it. */
  const Screenshot & shot() const { return mShot; }
  bool save(const std::filesystem::path & inFile) const;
  std::string instructions() const;
  bool active() const { return mActive; }
  bool isReady() const { return mReady; }

private:
  void setInstructions(const std::string & inText);
  static void TakeCB(Fl_Widget *, void *);
  static void SaveCB(Fl_Widget *, void *);
  static void CopyCB(Fl_Widget *, void *);

  Screenshot mShot;
  std::vector<uint8_t> mRGB;
  std::unique_ptr<Fl_RGB_Image> mImageData;
  bool mActive = false;           // screenshot mode asked for
  bool mReady = false;            // the Newton is ready
  bool mTaking = false;
};

} // namespace ncx

#endif // NCX_UI_PANELS_SCREENSHOTPANEL_H
