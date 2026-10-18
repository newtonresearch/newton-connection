/*
 File: Format.cc

 Numbers as the panels show them (Format.h).
 */

#include "ui/Format.h"

namespace ncx {

std::string GroupedNumber(int64_t inNumber)
{
  std::string digits = std::to_string(inNumber < 0 ? -inNumber : inNumber), text = inNumber < 0 ? "-" : "";
  for (size_t i = 0; i < digits.size(); ++i) {
    if (i > 0 && (digits.size() - i) % 3 == 0)
      text += ',';
    text += digits[i];
  }
  return text;
}


std::string SizeText(int64_t inBytes)
{
  return GroupedNumber(inBytes / 1024) + "K  (" + GroupedNumber(inBytes) + " bytes)";
}

} // namespace ncx
