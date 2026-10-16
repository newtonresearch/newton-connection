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
foreach (section strings newton dates folders prefs log threads progress observers)
  add_test (NAME ncx_base_${section} COMMAND ncx_base_test ${section})
endforeach ()
