#
# The app's images (resources/images/<name>.png: the device pictures, the
# sidebar's icons, ...) as a C++ source file, so that NCX has them without
# files next to it (PORTING.md 7.1). Run in script mode by src/CMakeLists.txt:
#
#   cmake -DDIR=<resources/images> -DOUT=<ImageData.cc> -P EmbedImages.cmake
#
# The generated file defines the table src/ui/Images.cc reads.
#

file (GLOB inputs "${DIR}/*.png")
list (SORT inputs)

set (arrays "")
set (table "")
set (count 0)
foreach (png IN LISTS inputs)
  get_filename_component (name "${png}" NAME_WE)
  file (READ "${png}" hex HEX)
  string (LENGTH "${hex}" length)
  math (EXPR size "${length} / 2")
  string (REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
  string (REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n  " bytes "${bytes}")
  string (APPEND arrays "static const unsigned char k${count}[] = {\n  ${bytes}\n};\n\n")
  string (APPEND table "  { \"${name}\", k${count}, ${size} },\n")
  math (EXPR count "${count} + 1")
endforeach ()

file (WRITE "${OUT}.tmp"
"// Generated from resources/images/*.png by cmake/EmbedImages.cmake.\n"
"// Don't edit.\n\n"
"#include \"ui/Images.h\"\n\n"
"namespace ncx {\n\n"
"${arrays}"
"extern const EmbeddedImage kEmbeddedImages[] = {\n${table}  { nullptr, nullptr, 0 }\n};\n\n"
"} // namespace ncx\n")
# only a change touches the file (and rebuilds what depends on it)
execute_process (COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OUT}.tmp" "${OUT}")
file (REMOVE "${OUT}.tmp")
