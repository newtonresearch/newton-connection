# ncx-cli backup against ncx-fake-newton (PORTING.md 5.2.4): a backup into
# a new .newtondevice file in a folder of its own, then what ncx-cli device
# says of it.
#
#   cmake -DCLI=<ncx-cli> -DFAKE=<ncx-fake-newton> -DPORT=<port> -DDIR=<folder> -P cli_backup.cmake

file (REMOVE_RECURSE "${DIR}")
execute_process (
  COMMAND "${FAKE}" --port ${PORT} --password test
  COMMAND "${CLI}" backup --folder "${DIR}" --target tcp:${PORT} --password test --wait 20
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
  RESULTS_VARIABLE results
  TIMEOUT 60
)
message ("${output}")
if (NOT results STREQUAL "0;0")
  file (REMOVE_RECURSE "${DIR}")
  message (FATAL_ERROR "exit codes ${results} (fake Newton; ncx-cli):\n${errors}")
endif ()
string (FIND "${output}" "4 soups, 5 entries came" found)
if (found LESS 0)
  file (REMOVE_RECURSE "${DIR}")
  message (FATAL_ERROR "ncx-cli didn't back up 4 soups and 5 entries:\n${errors}")
endif ()
file (GLOB files "${DIR}/*.newtondevice")
list (LENGTH files count)
if (NOT count EQUAL 1)
  file (REMOVE_RECURSE "${DIR}")
  message (FATAL_ERROR "${count} .newtondevice files in ${DIR}")
endif ()
execute_process (COMMAND "${CLI}" device ${files} --check OUTPUT_VARIABLE device RESULT_VARIABLE checked)
message ("${device}")
file (REMOVE_RECURSE "${DIR}")
string (FIND "${device}" "Total:    4 soups, 5 entries" found)
if (NOT checked EQUAL 0 OR found LESS 0)
  message (FATAL_ERROR "ncx-cli device doesn't read the backup back")
endif ()
