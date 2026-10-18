#
# NCX's tests (ctest). For now (PORTING.md 1.4): the core's self test, from
# ncx-cli and from the app (--selftest: no window, so no display needed).
#

add_test (NAME ncx_cli_selftest COMMAND ncx-cli --selftest)
add_test (NAME ncx_app_selftest COMMAND NCX --selftest)
add_test (NAME ncx_cli_version COMMAND ncx-cli --version)
set_tests_properties (ncx_cli_version PROPERTIES PASS_REGULAR_EXPRESSION "ncx-cli ${PROJECT_VERSION}")

# The base layer (PORTING.md Phase 2): one ctest per section of ncx_base_test
add_executable (ncx_base_test base_test.cc)
target_link_libraries (ncx_base_test PRIVATE ncx_core)
foreach (section strings newton dates folders serial prefs log threads progress observers)
  add_test (NAME ncx_base_${section} COMMAND ncx_base_test ${section})
endforeach ()

# The comms layer (PORTING.md Phase 3) against the fake Newton
add_library (ncx_fake_newton STATIC FakeNewton.cc FakeNewton.h)
target_link_libraries (ncx_fake_newton PUBLIC ncx_core)
target_include_directories (ncx_fake_newton PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_executable (ncx-fake-newton fake_newton_main.cc)
target_link_libraries (ncx-fake-newton PRIVATE ncx_fake_newton)
add_executable (ncx_comms_test comms_test.cc)
target_link_libraries (ncx_comms_test PRIVATE ncx_core)
add_dependencies (ncx_comms_test ncx-fake-newton)
foreach (section events mnp plain password listener package)
  add_test (NAME ncx_comms_${section} COMMAND ncx_comms_test ${section} $<TARGET_FILE:ncx-fake-newton>)
endforeach ()
# ncx-cli info against ncx-fake-newton: MNP (Einstein's serial port) and
# plain TCP (the Internet Enabler)
string (RANDOM LENGTH 3 ALPHABET 0123456789 port_base)
math (EXPR cli_port "43000 + ${port_base}")
math (EXPR cli_port_plain "43000 + ${port_base} + 1000")
add_test (NAME ncx_cli_info_mnp COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:ncx-cli> -DFAKE=$<TARGET_FILE:ncx-fake-newton>
          -DPORT=${cli_port} -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_info.cmake)
add_test (NAME ncx_cli_info_plain COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:ncx-cli> -DFAKE=$<TARGET_FILE:ncx-fake-newton>
          -DPORT=${cli_port_plain} -DPLAIN=ON -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_info.cmake)
math (EXPR cli_port_newton1 "43000 + ${port_base} + 2500")
add_test (NAME ncx_cli_info_newton1 COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:ncx-cli> -DFAKE=$<TARGET_FILE:ncx-fake-newton>
          -DPORT=${cli_port_newton1} -DNEWTON1=ON -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_info.cmake)

# The translators (PORTING.md Phase 6): vCard, iCalendar, text, packages
add_executable (ncx_translators_test translators_test.cc)
target_link_libraries (ncx_translators_test PRIVATE ncx_core)
foreach (section lines names dates notes builtin graphics richtext audio)
  add_test (NAME ncx_translators_${section} COMMAND ncx_translators_test ${section})
endforeach ()
add_test (NAME ncx_translators_transfer COMMAND ncx_translators_test transfer
          ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/einstein.newtondevice)
# a note drawn on Einstein: ink, polygons, ink text (no personal data)
add_test (NAME ncx_translators_ink COMMAND ncx_translators_test ink ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ink-note.nsof)
# Notes as RTF (the ink note), and Newton Works documents made on Einstein (eMate setup)
add_test (NAME ncx_translators_notesrtf COMMAND ncx_translators_test notesrtf ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
add_test (NAME ncx_translators_works COMMAND ncx_translators_test works ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
# NCX against what the Cocoa NCX exported of the same backup (fixtures/cocoa/README.md)
add_test (NAME ncx_translators_golden COMMAND ncx_translators_test golden ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
add_test (NAME ncx_cli_transfer COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:ncx-cli>
          -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/einstein.newtondevice
          -DDIR=${CMAKE_CURRENT_BINARY_DIR}/ncx_cli_transfer -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_transfer.cmake)

# The model (PORTING.md Phase 4): device files, entries, backups
add_executable (ncx_model_test model_test.cc)
target_link_libraries (ncx_model_test PRIVATE ncx_core)
foreach (section plist file delete entries titles backup devices large restore)
  add_test (NAME ncx_model_${section} COMMAND ncx_model_test ${section})
endforeach ()
# A backup the Cocoa NCX made from Einstein (no personal data): NCX reads it
# as that NCX wrote it, and changes it
add_test (NAME ncx_model_fixture COMMAND ncx_model_test fixture ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/einstein.newtondevice)

# The dock (PORTING.md Phase 5) against the fake Newton
add_executable (ncx_dock_test dock_test.cc)
target_link_libraries (ncx_dock_test PRIVATE ncx_fake_newton)
add_dependencies (ncx_dock_test ncx-fake-newton)
foreach (section extensions bits controller screenshot onepiece notoolkit memory packagefiles install browse backup deletions restore edit keyboard newton1 card)
  add_test (NAME ncx_dock_${section} COMMAND ncx_dock_test ${section} $<TARGET_FILE:ncx-fake-newton>)
endforeach ()
math (EXPR cli_port_screenshot "43000 + ${port_base} + 2000")
add_test (NAME ncx_cli_screenshot COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:ncx-cli> -DFAKE=$<TARGET_FILE:ncx-fake-newton>
          -DPORT=${cli_port_screenshot} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/ncx_cli_screenshot.png
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_screenshot.cmake)
math (EXPR cli_port_rom "43000 + ${port_base} + 3000")
add_test (NAME ncx_cli_rom COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:ncx-cli> -DFAKE=$<TARGET_FILE:ncx-fake-newton>
          -DPORT=${cli_port_rom} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/ncx_cli_rom.bin -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_rom.cmake)
math (EXPR cli_port_install "43000 + ${port_base} + 4000")
add_test (NAME ncx_cli_install COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:ncx-cli> -DFAKE=$<TARGET_FILE:ncx-fake-newton>
          -DMAKE=$<TARGET_FILE:ncx_dock_test> -DPORT=${cli_port_install} -DDIR=${CMAKE_CURRENT_BINARY_DIR}/ncx_cli_install
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_install.cmake)
math (EXPR cli_port_backup "43000 + ${port_base} + 5000")
add_test (NAME ncx_cli_backup COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:ncx-cli> -DFAKE=$<TARGET_FILE:ncx-fake-newton>
          -DPORT=${cli_port_backup} -DDIR=${CMAKE_CURRENT_BINARY_DIR}/ncx_cli_backup -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_backup.cmake)
math (EXPR cli_port_restore "43000 + ${port_base} + 6000")
add_test (NAME ncx_cli_restore COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:ncx-cli> -DFAKE=$<TARGET_FILE:ncx-fake-newton>
          -DPORT=${cli_port_restore} -DDIR=${CMAKE_CURRENT_BINARY_DIR}/ncx_cli_restore -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_restore.cmake)
math (EXPR cli_port_edit "43000 + ${port_base} + 500")
add_test (NAME ncx_cli_edit COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:ncx-cli> -DFAKE=$<TARGET_FILE:ncx-fake-newton>
          -DPORT=${cli_port_edit} -DDIR=${CMAKE_CURRENT_BINARY_DIR}/ncx_cli_edit -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_edit.cmake)
math (EXPR cli_port_type "43000 + ${port_base} + 1500")
add_test (NAME ncx_cli_type COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:ncx-cli> -DFAKE=$<TARGET_FILE:ncx-fake-newton>
          -DPORT=${cli_port_type} -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_type.cmake)
math (EXPR cli_port_card "43000 + ${port_base} + 3500")
add_test (NAME ncx_cli_card COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:ncx-cli> -DFAKE=$<TARGET_FILE:ncx-fake-newton>
          -DPORT=${cli_port_card} -DDIR=${CMAKE_CURRENT_BINARY_DIR}/ncx_cli_card -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_card.cmake)
