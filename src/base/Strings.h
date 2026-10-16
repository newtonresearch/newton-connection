/*
 File: Strings.h

 Text in NCX is UTF-8 in std::string (PORTING.md 2.1). Conversions to and
 from UTF-16 (the Newton's strings) and MacRoman (its symbols, as the Cocoa
 app read them), and the few string helpers the Cocoa app used
 (NSString-Extensions: Works' CSV and summaries). For Newton objects
 (RefArg) see NewtonStrings.h.
 */

#ifndef NCX_BASE_STRINGS_H
#define NCX_BASE_STRINGS_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace ncx {

/** UTF-16 to UTF-8. Surrogate pairs are joined; a lone surrogate becomes
    U+FFFD. */
std::string UTF8FromUTF16(const uint16_t * inChars, size_t inCount);
std::string UTF8FromUTF16(const std::u16string & inText);

/** UTF-8 to UTF-16. Code points above U+FFFF become surrogate pairs; bytes
    that aren't UTF-8 become U+FFFD (one per byte). */
std::u16string UTF16FromUTF8(const std::string & inText);

/** MacRoman to UTF-8 (0x80 ... 0xFF by Apple's table, 0xDB the euro sign,
    0xF0 the Apple logo U+F8FF). */
std::string UTF8FromMacRoman(const char * inText, size_t inLength);
std::string UTF8FromMacRoman(const std::string & inText);

/** Every inFrom replaced by inTo (NSString's
    stringByReplacingAllOccurrencesOfString:withString:). inFrom empty: the
    text as it is. */
std::string ReplaceAll(const std::string & inText, const std::string & inFrom, const std::string & inTo);

/** Every control character (U+0000 ... U+001F, U+007F ... U+009F) replaced by
    inWith (stringByReplacingCharactersInSet: controlCharacterSet). */
std::string ReplaceControlCharacters(const std::string & inText, const std::string & inWith);

/** Line ends as the Newton has them (CR), from LF, CR LF, or CR. */
std::string NewtonLineEnds(const std::string & inText);

/** Line ends as files on the desktop have them (LF), from CR or CR LF. */
std::string DesktopLineEnds(const std::string & inText);

} // namespace ncx

#endif // NCX_BASE_STRINGS_H
