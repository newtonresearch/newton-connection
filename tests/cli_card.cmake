# ncx-cli card against ncx-fake-newton (PORTING.md 5.2.9): its card in
# socket 0 (the CIS, the files), socket 1 empty.
#
#   cmake -DCLI=<ncx-cli> -DFAKE=<ncx-fake-newton> -DPORT=<port> -DDIR=<folder> -P cli_card.cmake

file (REMOVE_RECURSE "${DIR}")
execute_process (
  COMMAND "${FAKE}" --port ${PORT} --password test
  COMMAND "${CLI}" card --folder "${DIR}" --target tcp:${PORT} --password test --wait 20
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
  RESULTS_VARIABLE results
  TIMEOUT 60
)
message ("${output}")
if (NOT results STREQUAL "0;0")
  file (REMOVE_RECURSE "${DIR}")
  message (FATAL_ERROR "ncx-cli card: exit codes ${results} (fake Newton; ncx-cli):\n${errors}")
endif ()
foreach (expected "Socket 0 (0x30000000): a card, 5 V" "device: SRAM, 64 KB" "\"Test card\""
                  "card0-common.bin: 65536 bytes" "Socket 1 (0x40000000): no card")
  string (FIND "${output}" "${expected}" found)
  if (found LESS 0)
    file (REMOVE_RECURSE "${DIR}")
    message (FATAL_ERROR "ncx-cli card didn't print \"${expected}\":\n${errors}")
  endif ()
endforeach ()
foreach (name card0-cis.txt card0-attribute.bin card0-common.bin)
  if (NOT EXISTS "${DIR}/${name}")
    file (REMOVE_RECURSE "${DIR}")
    message (FATAL_ERROR "ncx-cli card didn't write ${name}")
  endif ()
endforeach ()
file (REMOVE_RECURSE "${DIR}")
