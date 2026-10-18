/*
 File: Canvas.cc

 Drawing into an image with FLTK (Canvas.h).
 */

#include "translators/Canvas.h"

#include <FL/Fl.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iterator>
#include <system_error>

namespace ncx {

int Pixels::gray(int inX, int inY) const
{
  if (inX < 0 || inY < 0 || inX >= width || inY >= height)
    return 255;
  const uint8_t * p = &rgb[((size_t)inY * width + inX) * 3];
  return (p[0] * 30 + p[1] * 59 + p[2] * 11) / 100;
}


bool Pixels::inkBounds(int & outLeft, int & outTop, int & outRight, int & outBottom) const
{
  outLeft = width;
  outTop = height;
  outRight = outBottom = 0;
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      if (gray(x, y) < 250) {
        outLeft = std::min(outLeft, x);
        outTop = std::min(outTop, y);
        outRight = std::max(outRight, x + 1);
        outBottom = std::max(outBottom, y + 1);
      }
  return outRight > outLeft;
}


struct Canvas::Surface
{
  Fl_Image_Surface surface;
  bool current = false;
  Surface(int inWidth, int inHeight) : surface(inWidth, inHeight) { }
};


Canvas::Canvas(int inWidth, int inHeight)
{
  Fl::lock();
  mSurface.reset(new Surface(inWidth > 0 ? inWidth : 1, inHeight > 0 ? inHeight : 1));
  Fl_Surface_Device::push_current(&mSurface->surface);
  mSurface->current = true;
  fl_color(FL_WHITE);
  fl_rectf(0, 0, inWidth, inHeight);
}


Canvas::~Canvas()
{
  if (mSurface && mSurface->current)
    Fl_Surface_Device::pop_current();
  mSurface.reset();
  Fl::unlock();
}


Pixels Canvas::take()
{
  Pixels pixels;
  if (!mSurface || !mSurface->current)
    return pixels;
  Fl_RGB_Image * image = mSurface->surface.image();
  Fl_Surface_Device::pop_current();
  mSurface->current = false;
  if (!image)
    return pixels;
  pixels.width = image->data_w();
  pixels.height = image->data_h();
  const uint8_t * data = (const uint8_t *)image->data()[0];
  int d = image->d(), ld = image->ld() ? image->ld() : pixels.width * d;
  pixels.rgb.resize((size_t)pixels.width * pixels.height * 3);
  for (int y = 0; y < pixels.height; ++y)
    for (int x = 0; x < pixels.width; ++x) {
      const uint8_t * p = data + (size_t)y * ld + (size_t)x * d;
      uint8_t * q = &pixels.rgb[((size_t)y * pixels.width + x) * 3];
      q[0] = p[0];
      q[1] = d >= 3 ? p[1] : p[0];
      q[2] = d >= 3 ? p[2] : p[0];
    }
  delete image;
  return pixels;
}


Pixels Crop(const Pixels & inPixels, int inLeft, int inTop, int inRight, int inBottom)
{
  inLeft = std::max(inLeft, 0);
  inTop = std::max(inTop, 0);
  inRight = std::min(inRight, inPixels.width);
  inBottom = std::min(inBottom, inPixels.height);
  Pixels part;
  if (inRight <= inLeft || inBottom <= inTop)
    return part;
  part.width = inRight - inLeft;
  part.height = inBottom - inTop;
  part.rgb.resize((size_t)part.width * part.height * 3);
  for (int y = 0; y < part.height; ++y)
    std::copy_n(&inPixels.rgb[((size_t)(inTop + y) * inPixels.width + inLeft) * 3], (size_t)part.width * 3,
                &part.rgb[(size_t)y * part.width * 3]);
  return part;
}


bool WritePNG(const std::filesystem::path & inFile, const Pixels & inPixels)
{
  if (inPixels.empty())
    return false;
  // fl_write_png takes a UTF-8 file name (FLTK's fl_fopen)
  std::string name = inFile.u8string();
  return fl_write_png(name.c_str(), (const char *)inPixels.rgb.data(), inPixels.width, inPixels.height, 3) == 0;
}


std::string PNGData(const Pixels & inPixels)
{
  // fl_write_png writes files only: one in the temp folder, read back
  static std::atomic<int> counter { 0 };
  std::error_code ec;
  std::filesystem::path file = std::filesystem::temp_directory_path(ec)
                               / ("ncx-png-" + std::to_string((long long)(uintptr_t)&counter) + "-" + std::to_string(++counter) + ".png");
  std::string data;
  if (WritePNG(file, inPixels)) {
    std::ifstream in(file, std::ios::binary);
    data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  std::filesystem::remove(file, ec);
  return data;
}


Pixels ReadPNG(const std::filesystem::path & inFile)
{
  Pixels pixels;
  std::string name = inFile.u8string();
  Fl_PNG_Image image(name.c_str());
  if (image.fail() || image.data_w() <= 0)
    return pixels;
  pixels.width = image.data_w();
  pixels.height = image.data_h();
  const uint8_t * data = (const uint8_t *)image.data()[0];
  int d = image.d(), ld = image.ld() ? image.ld() : pixels.width * d;
  bool alpha = d == 2 || d == 4;
  pixels.rgb.resize((size_t)pixels.width * pixels.height * 3);
  for (int y = 0; y < pixels.height; ++y)
    for (int x = 0; x < pixels.width; ++x) {
      const uint8_t * p = data + (size_t)y * ld + (size_t)x * d;
      uint8_t * q = &pixels.rgb[((size_t)y * pixels.width + x) * 3];
      q[0] = p[0];
      q[1] = d >= 3 ? p[1] : p[0];
      q[2] = d >= 3 ? p[2] : p[0];
      if (alpha) {
        // on white
        int a = p[d - 1];
        for (int c = 0; c < 3; ++c)
          q[c] = (uint8_t)((q[c] * a + 255 * (255 - a)) / 255);
      }
    }
  return pixels;
}

} // namespace ncx
