#
# NCX's code (PORTING.md section 2). The layers start with their phases:
# base/ (now), comms/, model/, dock/, translators/, platform/, ui/, cli/.
#

#------------------------------------------------------------------------------
#   ncx_core: everything but the UI. Its headers include no framework or FLTK
#   headers (they clash); newton_ncx links FLTK (its drawing), but nothing in
#   ncx_core needs a display.
#------------------------------------------------------------------------------
add_library (ncx_core STATIC
  base/Core.cc base/Core.h base/Version.h.in
)
target_include_directories (ncx_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_BINARY_DIR}/generated
)
target_link_libraries (ncx_core PUBLIC newton_ncx newton_dock ncx_sqlite3)

#------------------------------------------------------------------------------
#   ncx-cli: without a window, for tests and debugging.
#------------------------------------------------------------------------------
add_executable (ncx-cli cli/main.cc)
target_link_libraries (ncx-cli PRIVATE ncx_core)

#------------------------------------------------------------------------------
#   NCX: the app. NCX.app on macOS (Info.plist from cmake/), a GUI program on
#   Windows.
#------------------------------------------------------------------------------
set (ncx_icon "${PROJECT_SOURCE_DIR}/resources/NCX.icns")
add_executable (NCX WIN32 MACOSX_BUNDLE ui/main.cc ${ncx_icon})
target_link_libraries (NCX PRIVATE ncx_core fltk::fltk)
set_source_files_properties (${ncx_icon} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
set_target_properties (NCX PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/cmake/Info.plist.in"
  MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_DESCRIPTION}"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.newton.connection"
  MACOSX_BUNDLE_ICON_FILE "NCX.icns"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_COPYRIGHT "Newton Research Group"
)

# What CI uploads: cmake --install <build> --component ncx --prefix <dir>
install (TARGETS NCX ncx-cli
  BUNDLE DESTINATION . COMPONENT ncx
  RUNTIME DESTINATION bin COMPONENT ncx
)
