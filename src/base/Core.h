/*
 File: Core.h

 What the app and ncx-cli call to start the core (ncx_core) and to check that
 the libraries under it work. Plain C++: no newton-framework or FLTK headers
 here (they clash; only ncx_core's .cc files include the framework's).
 */

#ifndef NCX_BASE_CORE_H
#define NCX_BASE_CORE_H

#include <string>

namespace ncx {

/** "3.1.0" */
const char * Version();

/** Start the object system (newton_ncx: InitObjectSystemCore). Once; later
    calls do nothing. */
void InitCore();

/** Logging as the preferences say (LogLevel, LogToFile: the log file,
    AppFolders.h). */
void StartLogging();

/** Check the libraries: a frame through NSOF and back (newton_ncx), a frame
    for MNP and back (newton_dock), NSOF in an SQLite blob and back. Starts
    the core if needed. outReport gets a line per check, "ok ..." or
    "FAIL ...". True if all are ok. */
bool SelfTest(std::string & outReport);

} // namespace ncx

#endif // NCX_BASE_CORE_H
