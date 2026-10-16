#
# The protocol extensions NCX sends to a Newton (src/dock/extensions/
# <id>.nsof: the function as NSOF, its file name its 4-letter command) as a
# C++ source file, so that NCX has them without files next to it
# (PORTING.md 5.1). Run in script mode by src/CMakeLists.txt:
#
#   cmake -DDIR=<src/dock/extensions> -DOUT=<ExtensionData.cc> -P EmbedExtensions.cmake
#
# The generated file defines the table src/dock/Extensions.cc reads.
#

file (GLOB inputs "${DIR}/*.nsof")
list (SORT inputs)

set (arrays "")
set (table "")
set (count 0)
foreach (nsof IN LISTS inputs)
  get_filename_component (id "${nsof}" NAME_WE)
  string (LENGTH "${id}" idLength)
  if (NOT idLength EQUAL 4)
    message (FATAL_ERROR "${nsof}: an extension's name is its 4-letter command")
  endif ()
  file (READ "${nsof}" hex HEX)
  string (LENGTH "${hex}" length)
  math (EXPR size "${length} / 2")
  string (REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
  string (REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n  " bytes "${bytes}")
  string (APPEND arrays "static const unsigned char k${count}[] = {\n  ${bytes}\n};\n\n")
  string (APPEND table "  { \"${id}\", k${count}, ${size} },\n")
  math (EXPR count "${count} + 1")
endforeach ()

file (WRITE "${OUT}.tmp"
"// Generated from src/dock/extensions/*.nsof by cmake/EmbedExtensions.cmake.\n"
"// Don't edit.\n\n"
"#include \"dock/Extensions.h\"\n\n"
"namespace ncx {\n\n"
"${arrays}"
"extern const EmbeddedExtension kEmbeddedExtensions[] = {\n${table}  { nullptr, nullptr, 0 }\n};\n\n"
"} // namespace ncx\n")
# only a change touches the file (and rebuilds what depends on it)
execute_process (COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OUT}.tmp" "${OUT}")
file (REMOVE "${OUT}.tmp")
