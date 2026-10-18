/*
 File: Images.h

 The images built into the app (PORTING.md 7.1): resources/images/<name>.png,
 embedded by cmake/EmbedImages.cmake. The UI thread only.
 */

#ifndef NCX_UI_IMAGES_H
#define NCX_UI_IMAGES_H

#include <cstddef>

class Fl_Image;

namespace ncx {

struct EmbeddedImage
{
  const char * name;              // "device-mp2000"
  const unsigned char * data;     // the PNG
  size_t size;
};

/** An image by its name, made the first time (and kept); nullptr if there
    is none of that name. */
Fl_Image * BuiltinImage(const char * inName);

} // namespace ncx

#endif // NCX_UI_IMAGES_H
