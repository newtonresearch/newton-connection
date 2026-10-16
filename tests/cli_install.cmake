# ncx-cli install against ncx-fake-newton (PORTING.md 5.2.3): two packages,
# one of them installed already (the fake Newton says so of "Installed
# Package"): installed, and left as it is.
#
#   cmake -DCLI=<ncx-cli> -DFAKE=<ncx-fake-newton> -DMAKE=<ncx_dock_test> -DPORT=<port> -DDIR=<folder>
#         -P cli_install.cmake

file (MAKE_DIRECTORY "${DIR}")
execute_process (COMMAND "${MAKE}" make-package "${DIR}/new.newtonpkg" "New Package")
execute_process (COMMAND "${MAKE}" make-package "${DIR}/old.newtonpkg" "Installed Package")
execute_process (
  COMMAND "${FAKE}" --port ${PORT} --password test
  COMMAND "${CLI}" install "${DIR}/new.newtonpkg" "${DIR}/old.newtonpkg" --target tcp:${PORT} --password test --wait 20
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
  RESULTS_VARIABLE results
  TIMEOUT 60
)
message ("${output}")
file (REMOVE_RECURSE "${DIR}")
if (NOT results STREQUAL "0;0")
  message (FATAL_ERROR "exit codes ${results} (fake Newton; ncx-cli):\n${errors}")
endif ()
foreach (expected "new.newtonpkg: \"New Package\" installed" "old.newtonpkg: \"Installed Package\" is installed already")
  string (FIND "${output}" "${expected}" found)
  if (found LESS 0)
    message (FATAL_ERROR "ncx-cli didn't print \"${expected}\":\n${errors}")
  endif ()
endforeach ()
