/*
 File: PackageView.cc

 A package's info in the inspector (PackageView.h).
 */

#include "ui/panels/PackageView.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>

#include <algorithm>

namespace ncx {

namespace {

const int kMargin = 8;
const int kLabelWidth = 84;
const int kGap = 6;
const int kTextSize = 12;

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

} // namespace


PackageView::PackageView(int inX, int inY, int inW, int inH)
  : Fl_Scroll(inX, inY, inW, inH)
{
  type(Fl_Scroll::VERTICAL);
  box(FL_FLAT_BOX);
  color(FL_BACKGROUND2_COLOR);
  end();
}


PackageView::~PackageView()
{
  clear();      // the boxes use the icons
}


void PackageView::display(const PackageInfo & inInfo)
{
  mInfo = inInfo;
  mIcons.clear();
  for (const PackagePartInfo & part : mInfo.parts)
    if (!part.icon.empty())
      mIcons.emplace_back(new Fl_RGB_Image(part.icon.rgb.data(), part.icon.width, part.icon.height, 3));
  mLaidOutWidth = 0;
  layout();
}


void PackageView::resize(int inX, int inY, int inW, int inH)
{
  bool wider = inW != w();
  Fl_Scroll::resize(inX, inY, inW, inH);
  if (wider)
    layout();
}


void PackageView::layout()
{
  int width = w() - scrollbar.w();
  if (width == mLaidOutWidth && children() > 2)
    return;
  mLaidOutWidth = width;
  // the scrollbars stay, the rest goes
  clear();
  mRows.clear();
  begin();
  int top = y() + kMargin - yposition();
  int valueWidth = std::max(40, width - kLabelWidth - kGap - 2 * kMargin);
  auto section = [&](const std::string & inTitle) {
    top += 4;
    Fl_Box * box = new Fl_Box(x() + kMargin, top, width - 2 * kMargin, 20);
    box->copy_label(LabelText(inTitle).c_str());
    box->labelfont(FL_HELVETICA_BOLD);
    box->labelsize(kTextSize + 1);
    box->align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT | FL_ALIGN_CLIP);
    top += 22;
    mRows.push_back(inTitle);
  };
  auto row = [&](const char * inLabel, const std::string & inValue) {
    if (inValue.empty())
      return;
    std::string value = LabelText(inValue);
    fl_font(FL_HELVETICA, kTextSize);
    int mw = valueWidth, mh = 0;
    fl_measure(value.c_str(), mw, mh, 0);
    int height = std::max(18, mh + 2);
    Fl_Box * label = new Fl_Box(x() + kMargin, top, kLabelWidth, 18);
    label->label(inLabel);
    label->labelsize(kTextSize);
    label->labelcolor(fl_inactive(FL_FOREGROUND_COLOR));
    label->align(FL_ALIGN_INSIDE | FL_ALIGN_RIGHT | FL_ALIGN_TOP);
    Fl_Box * text = new Fl_Box(x() + kMargin + kLabelWidth + kGap, top, valueWidth, height);
    text->copy_label(value.c_str());
    text->labelsize(kTextSize);
    text->align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP);
    top += height + 2;
    mRows.push_back(std::string(inLabel) + " " + inValue);
  };
  if (!mInfo.error.empty()) {
    section("Not a package");
    row("Why:", mInfo.error);
  } else {
    section("Package Info");
    row("Name:", mInfo.name);
    row("Size:", mInfo.size);
    row("Created:", mInfo.created);
    row("Version:", mInfo.version);
    row("Copyright:", mInfo.copyright);
    if (mInfo.copyProtected)
      row("Protection:", "Copy protected");
    size_t icon = 0;
    for (const PackagePartInfo & part : mInfo.parts) {
      section(part.title);
      row("Size:", part.size);
      if (part.form) {
        if (!part.icon.empty() && icon < mIcons.size()) {
          Fl_Box * box = new Fl_Box(x() + kMargin + kLabelWidth + kGap, top, part.icon.width, part.icon.height);
          box->image(mIcons[icon++].get());
          box->align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT);
          top += part.icon.height + 4;
          mRows.push_back("Icon: " + std::to_string(part.icon.width) + " x " + std::to_string(part.icon.height));
        }
        row("Text:", part.text);
      }
      if (part.book) {
        row("Title:", part.bookTitle);
        row("ISBN:", part.isbn);
        row("Author:", part.author);
        row("Copyright:", part.copyright);
        row("Published:", part.published);
      }
    }
  }
  end();
  redraw();
}


std::vector<std::string> PackageView::rowTexts() const
{
  return mRows;
}

} // namespace ncx
