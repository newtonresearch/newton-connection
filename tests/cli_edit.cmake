# ncx-cli add and delete against ncx-fake-newton (PORTING.md 5.2.6): a
# backup first; then an entry by id and a package by name deleted, one
# that isn't there (exit 1), and add with nothing made on the desktop.
#
#   cmake -DCLI=<ncx-cli> -DFAKE=<ncx-fake-newton> -DPORT=<port> -DDIR=<folder> -P cli_edit.cmake

file (REMOVE_RECURSE "${DIR}")
function (dock what expectedResult expectedOutput)
  execute_process (
    COMMAND "${FAKE}" --port ${PORT} --password test
    COMMAND "${CLI}" ${what} --folder "${DIR}" --target tcp:${PORT} --password test --wait 20
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    RESULTS_VARIABLE results
    TIMEOUT 60
  )
  message ("${output}${errors}")
  if (NOT results STREQUAL "0;${expectedResult}")
    file (REMOVE_RECURSE "${DIR}")
    message (FATAL_ERROR "ncx-cli ${what}: exit codes ${results} (fake Newton; ncx-cli):\n${errors}")
  endif ()
  string (FIND "${output}${errors}" "${expectedOutput}" found)
  if (found LESS 0)
    file (REMOVE_RECURSE "${DIR}")
    message (FATAL_ERROR "ncx-cli ${what} didn't say \"${expectedOutput}\"")
  endif ()
endfunction ()

dock (backup 0 "4 soups, 5 entries came")
dock ("delete;Names;101" 0 "1 entries and 0 packages deleted on the Newton, 0 never sent deleted")
dock ("delete;Packages;Tetris" 0 "0 entries and 1 packages deleted on the Newton")
dock ("delete;Names;Nobody" 1 "Names has no entry Nobody")
dock (add 0 "0 entries made on the desktop sent to the Newton")
file (REMOVE_RECURSE "${DIR}")
