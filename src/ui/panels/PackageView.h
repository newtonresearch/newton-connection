/*
 File: PackageView.h

 A package's info in the inspector (PORTING.md 7.4; the Cocoa app's
 Package.storyboard): Package Info (name, size, created, version,
 copyright, copy protected), then a section a part (its size; a form's
 icon and text; a book's title, ISBN, author, copyright, publication date).
 The rows wrap to the inspector's width; laid out again when it changes.
 */

#ifndef NCX_UI_PANELS_PACKAGEVIEW_H
#define NCX_UI_PANELS_PACKAGEVIEW_H

#include "ui/PackageInfo.h"

#include <FL/Fl_Scroll.H>

#include <memory>
#include <string>
#include <vector>

class Fl_RGB_Image;

namespace ncx {

class PackageView : public Fl_Scroll
{
public:
  PackageView(int inX, int inY, int inW, int inH);
  ~PackageView() override;

  void display(const PackageInfo & inInfo);
  const PackageInfo & info() const { return mInfo; }
  /** The rows as text, "Name: Tetris" (tests). */
  std::vector<std::string> rowTexts() const;

  void resize(int inX, int inY, int inW, int inH) override;

private:
  void layout();

  PackageInfo mInfo;
  std::vector<std::unique_ptr<Fl_RGB_Image>> mIcons;
  std::vector<std::string> mRows;
  int mLaidOutWidth = 0;
};

} // namespace ncx

#endif // NCX_UI_PANELS_PACKAGEVIEW_H
