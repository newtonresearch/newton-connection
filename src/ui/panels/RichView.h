/*
 File: RichView.h

 A document in the inspector (PORTING.md 7.5): an EntryPreview's styled
 text and pictures, as HTML in an Fl_Help_View (fonts by the Newton's
 families, size, bold, italic, underline, alignment; pictures in place, as
 PNG files in a folder of its own, gone with the view). A text file shows
 as it is, in a fixed-width font.
 */

#ifndef NCX_UI_PANELS_RICHVIEW_H
#define NCX_UI_PANELS_RICHVIEW_H

#include "ui/EntryPreview.h"

#include <FL/Fl_Help_View.H>

#include <filesystem>
#include <string>

namespace ncx {

class RichView : public Fl_Help_View
{
public:
  RichView(int inX, int inY, int inW, int inH);
  ~RichView() override;

  void display(const EntryPreview & inPreview);
  /** The text shown (tests), and how many pictures. */
  const std::string & text() const { return mText; }
  int pictures() const { return mPictures; }

private:
  void clearPictures();

  std::filesystem::path mFolder;  // the pictures
  unsigned mGeneration = 0;
  std::string mText;
  int mPictures = 0;
};

/** The document as HTML for Fl_Help_View; pictures written into inFolder
    as <inPrefix>-<n>.png. outPictures: how many. */
std::string DocumentHTML(const EntryPreview & inPreview, const std::filesystem::path & inFolder,
                         const std::string & inPrefix, int & outPictures);

} // namespace ncx

#endif // NCX_UI_PANELS_RICHVIEW_H
