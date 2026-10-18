/*
 File: ChooserPanel.h

 The device chooser (PORTING.md 7.7): what the window waiting for a Newton
 shows. How to connect a Newton, and the backups in the app's folder (the
 owner's name, the Newton's id, when the file changed) to open one (Open,
 a double click), or one elsewhere (Other...). Its layout:
 ChooserPanelUI.fl.
 */

#ifndef NCX_UI_PANELS_CHOOSERPANEL_H
#define NCX_UI_PANELS_CHOOSERPANEL_H

#include "ChooserPanelUI.h"

#include "model/DeviceList.h"

#include <vector>

namespace ncx {

class ChooserPanel : public ChooserPanelUI
{
public:
  ChooserPanel(int inX, int inY, int inW, int inH);

  /** The backups there are now. */
  void update();
  const std::vector<DeviceFileInfo> & backups() const { return mFiles; }

private:
  static void OpenCB(Fl_Widget *, void *);
  static void OtherCB(Fl_Widget *, void *);
  static void BrowserCB(Fl_Widget *, void *);

  std::vector<DeviceFileInfo> mFiles;
};

} // namespace ncx

#endif // NCX_UI_PANELS_CHOOSERPANEL_H
