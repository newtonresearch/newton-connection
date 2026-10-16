#
# Newton Connection (NCX) on FLTK: Windows, Linux, macOS (PORTING.md).
#
#   cmake -B build
#   cmake --build build
#
# Everything it needs is fetched: FLTK, newton-framework, SQLite. For local
# work, use checkouts instead (nothing is downloaded for them then):
#
#   cmake -B build -DFETCHCONTENT_SOURCE_DIR_FLTK=~/dev/fltk-1.4.git \
#                  -DFETCHCONTENT_SOURCE_DIR_NEWTON=~/dev/newton-framework.matt.git
#
# Targets: NCX (the app; NCX.app on macOS), ncx-cli (the same without a
# window, for tests and debugging), ncx_core (what both link: everything but
# the UI). NCX/ and NCX.xcodeproj are the Cocoa app, reference only.
#

cmake_minimum_required (VERSION 3.16)

# macOS 11 and later (std::filesystem needs 10.15; the Info.plist says 11)
set (CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "The oldest macOS NCX runs on")

project (NCX
  VERSION 3.1.0          # the Cocoa app was 3.0.2
  DESCRIPTION "Newton Connection"
  LANGUAGES C CXX
)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)

option (NCX_BUILD_TESTS "Build and register NCX's tests (ctest)" ON)

include (FetchContent)

#------------------------------------------------------------------------------
#   FLTK: static libraries and FLUID (for the .fl files); no tests, examples,
#   OpenGL, or docs.
#------------------------------------------------------------------------------
set (FLTK_BUILD_SHARED_LIBS OFF)
set (FLTK_BUILD_FLUID ON)
set (FLTK_BUILD_FLTK_OPTIONS OFF)
set (FLTK_BUILD_TEST OFF)
set (FLTK_BUILD_EXAMPLES OFF)
set (FLTK_BUILD_FORMS OFF)
set (FLTK_BUILD_GL OFF)
set (FLTK_BUILD_HTML_DOCS OFF)
set (FLTK_BUILD_PDF_DOCS OFF)
FetchContent_Declare (fltk
  GIT_REPOSITORY https://github.com/fltk/fltk.git
  GIT_TAG release-1.4.5
  GIT_SHALLOW TRUE
)

#------------------------------------------------------------------------------
#   newton-framework: the libraries newton_ncx (object system, NSOF, stores,
#   packages, ROM data, QuickDraw on FLTK) and newton_dock (MNP, transports,
#   DES). As a subproject it builds nothing else; it draws with our FLTK
#   (NEWTON_NCX_FLTK follows fltk::fltk).
#------------------------------------------------------------------------------
FetchContent_Declare (newton
  GIT_REPOSITORY https://github.com/MatthiasWM/newton-framework.git
  GIT_TAG 8e5f9de4ac6709cb94c8856cddac772a78bece4e   # dap_to_toolbox, NCX 4: large binaries from NSOF
)

#------------------------------------------------------------------------------
#   SQLite, the amalgamation: the .newtondevice files (Core Data's SQLite
#   schema, PORTING.md 1.5). The hash is the one sqlite.org publishes.
#------------------------------------------------------------------------------
FetchContent_Declare (sqlite
  URL https://www.sqlite.org/2026/sqlite-amalgamation-3540000.zip
  URL_HASH SHA3_256=7b670a62fdfbd672b75fef004cb703c8a3e87d3a5cc7d675b4a08337004a2d93
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable (fltk)     # first: newton-framework uses it
FetchContent_MakeAvailable (newton sqlite)

add_library (ncx_sqlite3 STATIC "${sqlite_SOURCE_DIR}/sqlite3.c" "${sqlite_SOURCE_DIR}/sqlite3.h")
target_include_directories (ncx_sqlite3 PUBLIC "${sqlite_SOURCE_DIR}")
# no extensions loaded at run time (no dlopen); thread safe (the dock thread
# and the UI thread)
target_compile_definitions (ncx_sqlite3 PRIVATE SQLITE_OMIT_LOAD_EXTENSION=1 SQLITE_THREADSAFE=1 SQLITE_DQS=0)
find_package (Threads REQUIRED)
target_link_libraries (ncx_sqlite3 PUBLIC Threads::Threads)
if (UNIX AND NOT APPLE)
  target_link_libraries (ncx_sqlite3 PUBLIC m)
endif ()

#------------------------------------------------------------------------------
#   NCX
#------------------------------------------------------------------------------
configure_file (src/base/Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/base/Version.h" @ONLY)

add_subdirectory (src)

if (NCX_BUILD_TESTS)
  enable_testing ()
  add_subdirectory (tests)
endif ()
