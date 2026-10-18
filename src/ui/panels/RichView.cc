/*
 File: RichView.cc

 A document in the inspector (RichView.h).
 */

#include "ui/panels/RichView.h"

#include "platform/AppFolders.h"

#include <FL/Fl.H>
#include <FL/Fl_Shared_Image.H>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <random>

namespace ncx {

namespace {

std::string Escaped(const std::string & inText)
{
  std::string text;
  for (char c : inText)
    switch (c) {
      case '&': text += "&amp;"; break;
      case '<': text += "&lt;"; break;
      case '>': text += "&gt;"; break;
      case '"': text += "&quot;"; break;
      case '\t': text += "&nbsp;&nbsp;&nbsp;&nbsp;"; break;
      default: text += c;
    }
  return text;
}

/** Fl_Help_View's faces: helvetica, times, courier. */
const char * FaceOf(const TextStyle & inStyle)
{
  std::string font = inStyle.font;
  for (char & c : font)
    c = (char)std::tolower((unsigned char)c);
  if (font.find("courier") != std::string::npos || font.find("monaco") != std::string::npos)
    return "courier";
  if (font.find("schoolbook") != std::string::npos || font.find("times") != std::string::npos
      || font.find("new york") != std::string::npos)
    return "times";
  switch (inStyle.family) {
    case FontFamily::kFancy:
    case FontFamily::kHandwriting:
      return "times";
    default:
      return "helvetica";
  }
}

/** Points as HTML's sizes 1 ... 7 (3 the normal one). */
int HTMLSize(int inPoints)
{
  return std::max(1, std::min(7, 3 + (inPoints - 12) / 3));
}

} // namespace


std::string DocumentHTML(const EntryPreview & inPreview, const std::filesystem::path & inFolder,
                         const std::string & inPrefix, int & outPictures)
{
  outPictures = 0;
  std::string html = "<html><body bgcolor=\"#FFFFFF\">";
  if (inPreview.monospaced) {
    html += "<pre>";
    for (const RichParagraph & paragraph : inPreview.document.paragraphs)
      html += Escaped(paragraph.text()) + "\n";
    return html + "</pre></body></html>";
  }
  for (const RichParagraph & paragraph : inPreview.document.paragraphs) {
    const char * align = paragraph.style.alignment == Alignment::kCenter ? " align=\"center\""
                       : paragraph.style.alignment == Alignment::kRight ? " align=\"right\"" : "";
    html += std::string("<p") + align + ">";
    if (paragraph.runs.empty())
      html += "&nbsp;";
    for (const TextRun & run : paragraph.runs) {
      if (!run.png.empty()) {
        std::filesystem::path file = inFolder / (inPrefix + "-" + std::to_string(outPictures++) + ".png");
        std::ofstream(file, std::ios::binary).write(run.png.data(), (std::streamsize)run.png.size());
        html += "<img src=\"" + Escaped(UTF8FromPath(file)) + "\"";
        if (run.width > 0 && run.height > 0)
          html += " width=\"" + std::to_string(run.width) + "\" height=\"" + std::to_string(run.height) + "\"";
        html += ">";
        continue;
      }
      const TextStyle & style = run.style;
      std::string open = std::string("<font face=\"") + FaceOf(style) + "\" size=\"" + std::to_string(HTMLSize(style.size)) + "\">";
      std::string close = "</font>";
      if (style.bold) { open += "<b>"; close = "</b>" + close; }
      if (style.italic || style.family == FontFamily::kHandwriting) { open += "<i>"; close = "</i>" + close; }
      if (style.underline) { open += "<u>"; close = "</u>" + close; }
      html += open + Escaped(run.text) + close;
    }
    html += "</p>";
  }
  return html + "</body></html>";
}


RichView::RichView(int inX, int inY, int inW, int inH)
  : Fl_Help_View(inX, inY, inW, inH)
{
  static bool registered = false;
  if (!registered) {
    fl_register_images();         // <img>: PNG files through Fl_Shared_Image
    registered = true;
  }
  box(FL_FLAT_BOX);
  color(FL_BACKGROUND2_COLOR);
  textsize(13);
  static const unsigned process = std::random_device{}();
  mFolder = std::filesystem::temp_directory_path()
          / ("ncx-inspector-" + std::to_string(process) + "-" + std::to_string((uintptr_t)this));
}


RichView::~RichView()
{
  value("");
  clearPictures();
}


void RichView::clearPictures()
{
  std::error_code error;
  std::filesystem::remove_all(mFolder, error);
}


void RichView::display(const EntryPreview & inPreview)
{
  value("");
  clearPictures();
  std::error_code error;
  std::filesystem::create_directories(mFolder, error);
  // a name of its own each time: Fl_Shared_Image keeps images by name
  std::string prefix = "p" + std::to_string(++mGeneration);
  mText = inPreview.document.text();
  value(DocumentHTML(inPreview, mFolder, prefix, mPictures).c_str());
  topline(0);
}

} // namespace ncx
