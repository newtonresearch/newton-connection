#
# Files built into the app (PORTING.md 7.1, 7.9): resources/images/*.png
# (the device pictures, the sidebar's icons, ...) and resources/help/*.html
# (the help) as a C++ source file, so that NCX has them without files next
# to it. Run in script mode by src/CMakeLists.txt:
#
#   cmake -DDIR=<folder> -DGLOB=<*.png> -DTABLE=<kEmbeddedImages> -DOUT=<ImageData.cc> -P EmbedFiles.cmake
#
# The generated file defines the table (EmbeddedFile, src/ui/Images.h)
# that src/ui/Images.cc reads: a file's name without its extension, its
# bytes, how many (the bytes have a 0 after them, for text).
#

file (GLOB inputs "${DIR}/${GLOB}")
list (SORT inputs)

set (arrays "")
set (table "")
set (count 0)
foreach (input IN LISTS inputs)
  get_filename_component (name "${input}" NAME_WE)
  file (READ "${input}" hex HEX)
  string (LENGTH "${hex}" length)
  math (EXPR size "${length} / 2")
  string (REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
  string (REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n  " bytes "${bytes}")
  string (APPEND arrays "static const unsigned char k${count}[] = {\n  ${bytes}0x00\n};\n\n")
  string (APPEND table "  { \"${name}\", k${count}, ${size} },\n")
  math (EXPR count "${count} + 1")
endforeach ()

file (WRITE "${OUT}.tmp"
"// Generated from ${GLOB} by cmake/EmbedFiles.cmake.\n"
"// Don't edit.\n\n"
"#include \"ui/Images.h\"\n\n"
"namespace ncx {\n\n"
"${arrays}"
"extern const EmbeddedFile ${TABLE}[] = {\n${table}  { nullptr, nullptr, 0 }\n};\n\n"
"} // namespace ncx\n")
# only a change touches the file (and rebuilds what depends on it)
execute_process (COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OUT}.tmp" "${OUT}")
file (REMOVE "${OUT}.tmp")
