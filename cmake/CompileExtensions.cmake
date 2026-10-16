#
# Compile NCX's own protocol extensions (src/dock/extensions/<id>.ns,
# NewtonScript) with newtc into <id>.nsof next to them, which are checked
# in (as the framework's ROM data is: generated, then committed). The
# ncx_extensions target runs it when NCX_NEWTC names a newtc
# (newton-framework's, built with NEWTON_BUILD_TOOLS):
#
#   cmake -DNEWTC=<newtc> -DDIR=<src/dock/extensions> [-DEXTERNAL=<id>=<file.ns>;...]
#         -P CompileExtensions.cmake
#
# EXTERNAL: extensions whose source is elsewhere (newtdump's in
# newton-framework), compiled in their own directory (their Load()s).
#

file (GLOB sources "${DIR}/*.ns")
set (jobs "")
foreach (source IN LISTS sources)
  get_filename_component (id "${source}" NAME_WE)
  list (APPEND jobs "${id}=${source}")
endforeach ()
list (APPEND jobs ${EXTERNAL})
foreach (job IN LISTS jobs)
  string (FIND "${job}" "=" at)
  string (SUBSTRING "${job}" 0 ${at} id)
  math (EXPR at "${at} + 1")
  string (SUBSTRING "${job}" ${at} -1 source)
  get_filename_component (sourceDir "${source}" DIRECTORY)
  set (nsof "${DIR}/${id}.nsof")
  execute_process (
    COMMAND "${NEWTC}" -script "${source}" -onsof "${nsof}"
    WORKING_DIRECTORY "${sourceDir}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
  )
  if (NOT result EQUAL 0)
    message (FATAL_ERROR "newtc couldn't compile ${source}:\n${output}")
  endif ()
  message (STATUS "${id}.nsof from ${source}")
endforeach ()
