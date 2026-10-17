#
# NCX's code (PORTING.md section 2). The layers start with their phases:
# base/, comms/, platform/, ui/, cli/, model/, dock/ (now), translators/.
#

#------------------------------------------------------------------------------
#   ncx_core: everything but the UI. Its headers include no FLTK headers,
#   and only NewtonStrings.h includes the framework's (they clash). It links
#   FLTK for Fl_Preferences and through newton_ncx (its drawing), but nothing
#   in ncx_core needs a display.
#------------------------------------------------------------------------------
#------------------------------------------------------------------------------
#   The protocol extensions (src/dock/extensions/<id>.nsof) built into NCX.
#   ncx_extensions compiles the NewtonScript ones (<id>.ns, and newtdump's
#   NDmp from newton-framework) with newtc first, when NCX_NEWTC names one
#   (src/dock/extensions/README.md).
#------------------------------------------------------------------------------
file (GLOB ncx_extension_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/dock/extensions/*.nsof")
set (ncx_extension_data "${PROJECT_BINARY_DIR}/generated/ExtensionData.cc")
add_custom_command (
  OUTPUT "${ncx_extension_data}"
  COMMAND "${CMAKE_COMMAND}" -DDIR=${CMAKE_CURRENT_SOURCE_DIR}/dock/extensions -DOUT=${ncx_extension_data}
          -P "${PROJECT_SOURCE_DIR}/cmake/EmbedExtensions.cmake"
  DEPENDS ${ncx_extension_files} "${PROJECT_SOURCE_DIR}/cmake/EmbedExtensions.cmake"
  COMMENT "Embedding the protocol extensions"
  VERBATIM
)
set (NCX_NEWTC "" CACHE FILEPATH "newtc, to compile the protocol extensions (target ncx_extensions)")
if (NCX_NEWTC)
  add_custom_target (ncx_extensions
    COMMAND "${CMAKE_COMMAND}" -DNEWTC=${NCX_NEWTC} -DDIR=${CMAKE_CURRENT_SOURCE_DIR}/dock/extensions
            "-DEXTERNAL=NDmp=${newton_SOURCE_DIR}/Matt/NewtDump/Newton/Extension.ns"
            -P "${PROJECT_SOURCE_DIR}/cmake/CompileExtensions.cmake"
    COMMENT "Compiling the protocol extensions with newtc"
    VERBATIM
  )
endif ()

add_library (ncx_core STATIC
  base/Core.cc base/Core.h base/Version.h.in
  base/Dates.cc base/Dates.h
  base/Log.cc base/Log.h
  base/NewtonCall.cc base/NewtonCall.h
  base/NewtonStrings.cc base/NewtonStrings.h
  base/Observers.cc base/Observers.h
  base/Prefs.cc base/Prefs.h
  base/Progress.h
  base/Strings.cc base/Strings.h
  base/Threads.cc base/Threads.h
  comms/Connection.cc comms/Connection.h
  comms/DockEvent.cc comms/DockEvent.h comms/DockProtocol.h
  comms/DockRefs.cc comms/DockRefs.h
  comms/EventQueue.cc comms/EventQueue.h
  comms/Handshake.cc comms/Handshake.h
  comms/Listener.cc comms/Listener.h
  comms/Session.cc comms/Session.h
  dock/Backup.cc dock/Backup.h
  dock/DockController.cc dock/DockController.h
  dock/Extensions.cc dock/Extensions.h ${ncx_extension_data}
  dock/FileBrowser.cc dock/FileBrowser.h
  dock/MemoryDump.cc dock/MemoryDump.h
  dock/Packages.cc dock/Packages.h
  dock/Restore.cc dock/Restore.h
  dock/Screenshot.cc dock/Screenshot.h
  dock/SoupEditor.cc dock/SoupEditor.h
  model/BackupImport.cc model/BackupImport.h
  model/CoreDataModel.cc model/CoreDataModel.h
  model/DeviceFile.cc model/DeviceFile.h
  model/DeviceList.cc model/DeviceList.h
  model/NewtonModel.cc model/NewtonModel.h
  model/Plist.cc model/Plist.h
  model/Records.cc model/Records.h
  model/Restore.cc model/Restore.h
  platform/AppFolders.cc platform/AppFolders.h
  platform/SerialPorts.cc platform/SerialPorts.h
)
target_include_directories (ncx_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_BINARY_DIR}/generated
)
# fltk::fltk: Fl_Preferences (Prefs.cc)
target_link_libraries (ncx_core PUBLIC newton_ncx newton_dock ncx_sqlite3 fltk::fltk)

#------------------------------------------------------------------------------
#   ncx-cli: without a window, for tests and debugging.
#------------------------------------------------------------------------------
add_executable (ncx-cli cli/main.cc)
target_link_libraries (ncx-cli PRIVATE ncx_core)    # fl_write_png: fltk::images comes with newton_ncx

#------------------------------------------------------------------------------
#   NCX: the app. NCX.app on macOS (Info.plist from cmake/), a GUI program on
#   Windows.
#------------------------------------------------------------------------------
set (ncx_icon "${PROJECT_SOURCE_DIR}/resources/NCX.icns")
add_executable (NCX WIN32 MACOSX_BUNDLE ui/main.cc ${ncx_icon})
target_link_libraries (NCX PRIVATE ncx_core)    # FLTK comes with it
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
