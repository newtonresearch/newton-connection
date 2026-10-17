# ncx-cli type against ncx-fake-newton (PORTING.md 5.2.7): keyboard
# passthrough, text from the command line with a return.
#
#   cmake -DCLI=<ncx-cli> -DFAKE=<ncx-fake-newton> -DPORT=<port> -P cli_type.cmake

execute_process (
  COMMAND "${FAKE}" --port ${PORT} --password test
  COMMAND "${CLI}" type "Hello" "Newton\\nbye" --target tcp:${PORT} --password test --wait 20
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
  RESULTS_VARIABLE results
  TIMEOUT 60
)
message ("${output}${errors}")
if (NOT results STREQUAL "0;0")
  message (FATAL_ERROR "ncx-cli type: exit codes ${results} (fake Newton; ncx-cli):\n${errors}")
endif ()
string (FIND "${output}" "16 characters typed" found)
if (found LESS 0)
  message (FATAL_ERROR "ncx-cli type didn't type 16 characters")
endif ()
