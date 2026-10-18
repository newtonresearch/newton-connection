/*
 File: Canvas.h

 Drawing into an image with FLTK (PORTING.md 6.3): the framework's
 QuickDraw draws on FLTK's current surface (newton-framework 0.5), so a
 Canvas makes an image surface current, white, as big as asked, and gives
 its pixels back when done. The pixels become PNG files (fl_write_png).

 FLTK isn't thread-safe: a Canvas holds Fl::lock() while it lives (the
 translators run on the dock thread; the app's main thread draws windows).
 On Linux an image surface needs a display (X11 or Wayland; xvfb-run in
 CI).

 No framework headers here (they clash with FLTK's); Render.h draws Newton
 objects on a Canvas.
 */

#ifndef NCX_TRANSLATORS_CANVAS_H
#define NCX_TRANSLATORS_CANVAS_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ncx {

/** RGB pixels, a row after the other, 3 bytes a pixel. */
struct Pixels
{
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgb;

  bool empty() const { return width <= 0 || height <= 0; }
  /** A pixel's gray (0 black ... 255 white), 255 outside. */
  int gray(int inX, int inY) const;
  /** The smallest rectangle with the pixels that aren't white: false if
      all of them are. */
  bool inkBounds(int & outLeft, int & outTop, int & outRight, int & outBottom) const;
};

class Canvas
{
public:
  /** inWidth by inHeight pixels, white, the current surface. */
  Canvas(int inWidth, int inHeight);
  ~Canvas();
  Canvas(const Canvas &) = delete;
  Canvas & operator=(const Canvas &) = delete;

  /** What was drawn; the surface is no longer current. */
  Pixels take();

private:
  struct Surface;
  std::unique_ptr<Surface> mSurface;
};

/** A part of the pixels (clipped to them). */
Pixels Crop(const Pixels & inPixels, int inLeft, int inTop, int inRight, int inBottom);

/** The pixels as a PNG file; false if it can't be written. */
bool WritePNG(const std::filesystem::path & inFile, const Pixels & inPixels);
/** The pixels as PNG data ("" if they can't be encoded). */
std::string PNGData(const Pixels & inPixels);
/** A PNG file's pixels, transparent ones on white (empty if it isn't
    one). */
Pixels ReadPNG(const std::filesystem::path & inFile);

} // namespace ncx

#endif // NCX_TRANSLATORS_CANVAS_H
