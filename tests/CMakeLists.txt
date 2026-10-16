#
# NCX's tests (ctest). For now (PORTING.md 1.4): the core's self test, from
# ncx-cli and from the app (--selftest: no window, so no display needed).
#

add_test (NAME ncx_cli_selftest COMMAND ncx-cli --selftest)
add_test (NAME ncx_app_selftest COMMAND NCX --selftest)
add_test (NAME ncx_cli_version COMMAND ncx-cli --version)
set_tests_properties (ncx_cli_version PROPERTIES PASS_REGULAR_EXPRESSION "ncx-cli ${PROJECT_VERSION}")
