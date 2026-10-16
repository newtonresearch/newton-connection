/*
 File: NewtonCall.cc

 Newton exceptions and C++ exceptions together (NewtonCall.h).
 */

#include "base/NewtonCall.h"

#include "Objects.h"

#include <exception>

namespace ncx {

void CallNewton(const std::function<void()> & inCode)
{
  std::exception_ptr cppException;
  bool newtonException = false;
  long error = 0;
  std::string name;
  newton_try
  {
    // a C++ exception stops here, inside the block, so end_try still takes
    // the handler out of the list; it goes on below
    try {
      inCode();
    } catch (...) {
      cppException = std::current_exception();
    }
  }
  newton_catch_all
  {
    newtonException = true;
    error = (long)(intptr_t)CurrentException()->data;
    name = CurrentException()->name ? CurrentException()->name : "";
  }
  end_try;
  if (cppException)
    std::rethrow_exception(cppException);
  if (newtonException)
    throw NewtonException(error, name);
}

} // namespace ncx
