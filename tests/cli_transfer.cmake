# ncx-cli export and import (PORTING.md 6.2) on a copy of the Einstein
# fixture: Names as a .vcf, Dates as an .ics, packages; both imported back.
#
#   cmake -DCLI=<ncx-cli> -DFIXTURE=<einstein.newtondevice> -DDIR=<folder> -P cli_transfer.cmake

file (REMOVE_RECURSE "${DIR}")
file (MAKE_DIRECTORY "${DIR}")
file (COPY "${FIXTURE}" DESTINATION "${DIR}")
get_filename_component (fixture_name "${FIXTURE}" NAME)
file (RENAME "${DIR}/${fixture_name}" "${DIR}/e.newtondevice")
function (run expected)
  execute_process (COMMAND "${CLI}" ${ARGN} OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE result)
  message ("${output}${errors}")
  if (NOT result EQUAL 0)
    file (REMOVE_RECURSE "${DIR}")
    message (FATAL_ERROR "ncx-cli ${ARGN}: exit code ${result}")
  endif ()
  foreach (text ${expected})
    string (FIND "${output}" "${text}" found)
    if (found LESS 0)
      file (REMOVE_RECURSE "${DIR}")
      message (FATAL_ERROR "ncx-cli ${ARGN} didn't print \"${text}\"")
    endif ()
  endforeach ()
endfunction ()

run ("Names: 8 entries -> Newton Names.vcf; Newton Names.text;;Dates: 3 entries -> Newton Dates.ics;;Monaco.newtonpkg"
     export "${DIR}/e.newtondevice" --to "${DIR}/out")
run ("6 entries into Names: Names 6;;3 entries into Dates: Calendar 3;"
     import "${DIR}/e.newtondevice" "${DIR}/out/Newton Names.vcf" "${DIR}/out/Newton Dates.ics")
file (REMOVE_RECURSE "${DIR}")
