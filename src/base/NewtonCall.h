/*
 File: NewtonCall.h

 Newton exceptions and C++ exceptions together. newton-framework throws
 with longjmp (Throw, newton_try ... end_try): a C++ exception must not
 leave a newton_try block (its handler would stay in the framework's list
 and a later Throw would jump into a stack frame that is gone), and a
 Throw no newton_try catches ends the program ("Unhandled exception ...
 warm reboot!"). CallNewton runs code where both are safe: a Newton
 exception comes out as a NewtonException, a C++ exception as itself.
 The dock thread runs every operation through it.
 */

#ifndef NCX_BASE_NEWTONCALL_H
#define NCX_BASE_NEWTONCALL_H

#include <functional>
#include <stdexcept>
#include <string>

namespace ncx {

/** A Newton exception (the framework's Throw). */
struct NewtonException : std::runtime_error
{
  long error;                 // the exception's data as an error number (0 if none)
  std::string name;           // "evt.ex.fr.type;type.ref.frame"
  NewtonException(long inError, const std::string & inName)
    : std::runtime_error("Newton exception " + inName + " (" + std::to_string(inError) + ")"), error(inError), name(inName) { }
};

/** Run inCode; a Newton exception in it throws a NewtonException, a C++
    exception goes on as it is, the framework's handlers left in order. */
void CallNewton(const std::function<void()> & inCode);

} // namespace ncx

#endif // NCX_BASE_NEWTONCALL_H
