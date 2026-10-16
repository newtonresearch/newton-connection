# ncx-cli restore against ncx-fake-newton (PORTING.md 5.2.5): a backup
# first, then restore without --yes (nothing done, exit 1), then with it.
#
#   cmake -DCLI=<ncx-cli> -DFAKE=<ncx-fake-newton> -DPORT=<port> -DDIR=<folder> -P cli_restore.cmake

file (REMOVE_RECURSE "${DIR}")
function (dock what expectedResult)
  execute_process (
    COMMAND "${FAKE}" --port ${PORT} --password test
    COMMAND "${CLI}" ${what} --folder "${DIR}" --target tcp:${PORT} --password test --wait 20
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    RESULTS_VARIABLE results
    TIMEOUT 60
  )
  message ("${output}")
  if (NOT results STREQUAL "0;${expectedResult}")
    file (REMOVE_RECURSE "${DIR}")
    message (FATAL_ERROR "ncx-cli ${what}: exit codes ${results} (fake Newton; ncx-cli):\n${errors}")
  endif ()
  set (output "${output}" PARENT_SCOPE)
endfunction ()

dock (backup 0)
dock (restore 1)
string (FIND "${output}" "Nothing restored" found)
if (found LESS 0)
  file (REMOVE_RECURSE "${DIR}")
  message (FATAL_ERROR "ncx-cli restore without --yes didn't say it restored nothing")
endif ()
dock ("restore;--packages;--yes" 0)
file (REMOVE_RECURSE "${DIR}")
string (FIND "${output}" "3 soups, 4 entries, 1 packages restored" found)
if (found LESS 0)
  message (FATAL_ERROR "ncx-cli restore didn't restore 3 soups, 4 entries and a package")
endif ()
