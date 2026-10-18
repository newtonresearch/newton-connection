/*
 File: Images.h

 The files built into the app (PORTING.md 7.1, 7.9): the images
 (resources/images/<name>.png) and the help (resources/help/<name>.html),
 embedded by cmake/EmbedFiles.cmake. The UI thread only.
 */

#ifndef NCX_UI_IMAGES_H
#define NCX_UI_IMAGES_H

#include <cstddef>

class Fl_Image;

namespace ncx {

struct EmbeddedFile
{
  const char * name;              // "device-mp2000", without the extension
  const unsigned char * data;     // the file's bytes, a 0 after them
  size_t size;
};

/** An image by its name, made the first time (and kept); nullptr if there
    is none of that name. */
Fl_Image * BuiltinImage(const char * inName);

/** A help file by its name (resources/help/<name>.html): its text, "" if
    there is none. */
const char * BuiltinHelp(const char * inName);

} // namespace ncx

#endif // NCX_UI_IMAGES_H
