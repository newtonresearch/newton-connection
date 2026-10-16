/*
 File: NewtonStrings.h

 Newton strings and symbols as UTF-8 and back: what MakeNSString,
 MakeString(NSString *) and [NSString stringWithCString:SymbolName(...)
 encoding:NSMacOSRomanStringEncoding] did in the Cocoa app.

 This header includes newton-framework's (Objects.h): for ncx_core's code,
 not for the UI (FLTK's headers clash with the framework's).
 */

#ifndef NCX_BASE_NEWTONSTRINGS_H
#define NCX_BASE_NEWTONSTRINGS_H

#include "Objects.h"

#include <string>

namespace ncx {

/** A Newton string (class 'string or a subclass) as UTF-8, without its
    terminating 0; line ends stay as they are (CR). "" if it isn't a
    string. */
std::string UTF8FromString(RefArg inStr);

/** The same, with line ends as on the desktop (LF). */
std::string UTF8FromStringLF(RefArg inStr);

/** A Newton string from UTF-8, its line ends as the Newton has them (CR;
    MakeString turned LF into CR). */
Ref MakeStringFromUTF8(const std::string & inText);

/** A symbol's name as UTF-8 (the Cocoa app read them as MacRoman). "" if
    it isn't a symbol. */
std::string UTF8FromSymbol(RefArg inSym);

} // namespace ncx

#endif // NCX_BASE_NEWTONSTRINGS_H
