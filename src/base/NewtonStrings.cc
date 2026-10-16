/*
 File: NewtonStrings.cc

 Newton strings and symbols as UTF-8 and back (NewtonStrings.h).
 */

#include "base/NewtonStrings.h"
#include "base/Strings.h"

#include "RSSymbols.h"

#include <cstring>

namespace ncx {

std::string UTF8FromString(RefArg inStr)
{
  if (!IsString(inStr))
    return std::string();
  const UniChar * chars = GetUString(inStr);
  size_t count = Length(inStr) / sizeof(UniChar);
  while (count > 0 && chars[count - 1] == 0)   // the terminating 0
    --count;
  return UTF8FromUTF16(chars, count);
}


std::string UTF8FromStringLF(RefArg inStr)
{
  return DesktopLineEnds(UTF8FromString(inStr));
}


Ref MakeStringFromUTF8(const std::string & inText)
{
  std::u16string chars = UTF16FromUTF8(NewtonLineEnds(inText));
  // a string in memory: UniChars in the host's byte order, then a 0
  RefVar str(AllocateBinary(SYMA(string), (chars.size() + 1) * sizeof(UniChar)));
  UniChar * p = (UniChar *)BinaryData(str);
  std::memcpy(p, chars.data(), chars.size() * sizeof(UniChar));
  p[chars.size()] = 0;
  return str;
}


std::string UTF8FromSymbol(RefArg inSym)
{
  if (!IsSymbol(inSym))
    return std::string();
  return UTF8FromMacRoman(SymbolName(inSym));
}

} // namespace ncx
