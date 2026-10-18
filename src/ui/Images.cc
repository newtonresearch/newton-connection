/*
 File: Images.cc

 The files built into the app (Images.h).
 */

#include "ui/Images.h"

#include <FL/Fl_PNG_Image.H>

#include <cstring>
#include <map>
#include <string>

namespace ncx {

extern const EmbeddedFile kEmbeddedImages[];       // ImageData.cc, generated
extern const EmbeddedFile kEmbeddedHelp[];         // HelpData.cc, generated

Fl_Image * BuiltinImage(const char * inName)
{
  static std::map<std::string, Fl_Image *> made;
  auto found = made.find(inName);
  if (found != made.end())
    return found->second;
  Fl_Image * image = nullptr;
  for (const EmbeddedFile * each = kEmbeddedImages; each->name; ++each)
    if (std::strcmp(each->name, inName) == 0) {
      image = new Fl_PNG_Image(each->name, each->data, (int)each->size);
      if (image->fail()) {
        delete image;
        image = nullptr;
      }
      break;
    }
  made[inName] = image;
  return image;
}

const char * BuiltinHelp(const char * inName)
{
  for (const EmbeddedFile * each = kEmbeddedHelp; each->name; ++each)
    if (std::strcmp(each->name, inName) == 0)
      return (const char *)each->data;
  return "";
}

} // namespace ncx
