# ncx-cli screenshot against ncx-fake-newton (PORTING.md 5.2.1): both at
# once, then the PNG it wrote.
#
#   cmake -DCLI=<ncx-cli> -DFAKE=<ncx-fake-newton> -DPORT=<port> -DOUT=<file.png> -P cli_screenshot.cmake

file (REMOVE "${OUT}")
execute_process (
  COMMAND "${FAKE}" --port ${PORT} --password test
  COMMAND "${CLI}" screenshot "${OUT}" --target tcp:${PORT} --password test --wait 20
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
  RESULTS_VARIABLE results
  TIMEOUT 60
)
message ("${output}")
if (NOT results STREQUAL "0;0")
  message (FATAL_ERROR "exit codes ${results} (fake Newton; ncx-cli):\n${errors}")
endif ()
string (FIND "${output}" "320 x 480, 4 bits a pixel on the Newton" found)
if (found LESS 0)
  message (FATAL_ERROR "ncx-cli didn't tell the screenshot's size:\n${errors}")
endif ()
file (READ "${OUT}" head LIMIT 8 HEX)
if (NOT head STREQUAL "89504e470d0a1a0a")
  message (FATAL_ERROR "${OUT} isn't a PNG")
endif ()
file (REMOVE "${OUT}")
