# ncx-cli info against ncx-fake-newton (PORTING.md 3.6, 3.7): both at once
# (the fake Newton keeps trying to connect while ncx-cli starts listening),
# then what ncx-cli printed.
#
#   cmake -DCLI=<ncx-cli> -DFAKE=<ncx-fake-newton> -DPORT=<port> [-DPLAIN=ON] [-DNEWTON1=ON] -P cli_info.cmake

set (rom "ROM:      2.2")
if (PLAIN)
  set (plain_arg --plain)
endif ()
if (NEWTON1)
  set (plain_arg --newton1)
  set (rom "ROM:      1.3 (NewtonOS 1)")
endif ()
execute_process (
  COMMAND "${FAKE}" --port ${PORT} --password test ${plain_arg}
  COMMAND "${CLI}" info --target tcp:${PORT} --password test --wait 20
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
  RESULTS_VARIABLE results
  TIMEOUT 60
)
message ("${output}")
if (NOT results STREQUAL "0;0")
  message (FATAL_ERROR "exit codes ${results} (fake Newton; ncx-cli):\n${errors}")
endif ()
foreach (expected "Newton:   Ada Lovelace" "Machine:  MessagePad 2000/2100" "${rom}"
                  "Store:    Internal (Internal), signature 1234567, 262144 of 1048576 bytes used"
                  "Soups:    Names, Notes")
  string (FIND "${output}" "${expected}" found)
  if (found LESS 0)
    message (FATAL_ERROR "ncx-cli didn't print \"${expected}\":\n${errors}")
  endif ()
endforeach ()
