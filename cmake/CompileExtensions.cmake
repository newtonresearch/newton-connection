#
# Compile NCX's own protocol extensions (src/dock/extensions/<id>.ns,
# NewtonScript) with newtc into <id>.nsof next to them, which are checked
# in (as the framework's ROM data is: generated, then committed). The
# ncx_extensions target runs it when NCX_NEWTC names a newtc
# (newton-framework's, built with NEWTON_BUILD_TOOLS):
#
#   cmake -DNEWTC=<newtc> -DDIR=<src/dock/extensions> -P CompileExtensions.cmake
#

file (GLOB sources "${DIR}/*.ns")
foreach (source IN LISTS sources)
  get_filename_component (id "${source}" NAME_WE)
  set (nsof "${DIR}/${id}.nsof")
  execute_process (
    COMMAND "${NEWTC}" -script "${source}" -onsof "${nsof}"
    WORKING_DIRECTORY "${DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
  )
  if (NOT result EQUAL 0)
    message (FATAL_ERROR "newtc couldn't compile ${source}:\n${output}")
  endif ()
  message (STATUS "${id}.nsof from ${id}.ns")
endforeach ()
