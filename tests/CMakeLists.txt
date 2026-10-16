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
foreach (section extensions bits controller screenshot onepiece notoolkit memory)
  add_test (NAME ncx_dock_${section} COMMAND ncx_dock_test ${section} $<TARGET_FILE:ncx-fake-newton>)
endforeach ()
math (EXPR cli_port_screenshot "43000 + ${port_base} + 2000")
add_test (NAME ncx_cli_screenshot COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:ncx-cli> -DFAKE=$<TARGET_FILE:ncx-fake-newton>
          -DPORT=${cli_port_screenshot} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/ncx_cli_screenshot.png
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_screenshot.cmake)
math (EXPR cli_port_rom "43000 + ${port_base} + 3000")
add_test (NAME ncx_cli_rom COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:ncx-cli> -DFAKE=$<TARGET_FILE:ncx-fake-newton>
          -DPORT=${cli_port_rom} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/ncx_cli_rom.bin -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_rom.cmake)
