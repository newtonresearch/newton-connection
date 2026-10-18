/*
 File: Format.h

 Numbers as the panels show them (the Cocoa app's gNumberFormatter, en_US).
 */

#ifndef NCX_UI_FORMAT_H
#define NCX_UI_FORMAT_H

#include <cstdint>
#include <string>

namespace ncx {

/** 8208384 as "8,208,384". */
std::string GroupedNumber(int64_t inNumber);

/** A size as the store panel has it: "8,016K  (8,208,384 bytes)". */
std::string SizeText(int64_t inBytes);

} // namespace ncx

#endif // NCX_UI_FORMAT_H
