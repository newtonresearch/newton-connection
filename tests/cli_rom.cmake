# ncx-cli rom against ncx-fake-newton (PORTING.md 5.2.2): both at once,
# then the file it wrote (64 KB of the fake Newton's memory).
#
#   cmake -DCLI=<ncx-cli> -DFAKE=<ncx-fake-newton> -DPORT=<port> -DOUT=<file> -P cli_rom.cmake

file (REMOVE "${OUT}")
execute_process (
  COMMAND "${FAKE}" --port ${PORT} --password test
  COMMAND "${CLI}" rom "${OUT}" --size 65536 --target tcp:${PORT} --password test --wait 20
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
  RESULTS_VARIABLE results
  TIMEOUT 60
)
message ("${output}")
if (NOT results STREQUAL "0;0")
  message (FATAL_ERROR "exit codes ${results} (fake Newton; ncx-cli):\n${errors}")
endif ()
file (SIZE "${OUT}" size)
if (NOT size EQUAL 65536)
  message (FATAL_ERROR "${OUT}: ${size} bytes, not 65536")
endif ()
file (REMOVE "${OUT}")
