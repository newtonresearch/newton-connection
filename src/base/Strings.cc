/*
 File: Strings.cc

 UTF-8, UTF-16, MacRoman, and string helpers (Strings.h).
 */

#include "base/Strings.h"

namespace ncx {

namespace {

const char32_t kReplacement = 0xFFFD;

void AppendUTF8(std::string & ioText, char32_t inCode)
{
  if (inCode < 0x80) {
    ioText += char(inCode);
  } else if (inCode < 0x800) {
    ioText += char(0xC0 | (inCode >> 6));
    ioText += char(0x80 | (inCode & 0x3F));
  } else if (inCode < 0x10000) {
    ioText += char(0xE0 | (inCode >> 12));
    ioText += char(0x80 | ((inCode >> 6) & 0x3F));
    ioText += char(0x80 | (inCode & 0x3F));
  } else {
    ioText += char(0xF0 | (inCode >> 18));
    ioText += char(0x80 | ((inCode >> 12) & 0x3F));
    ioText += char(0x80 | ((inCode >> 6) & 0x3F));
    ioText += char(0x80 | (inCode & 0x3F));
  }
}

// MacRoman 0x80 ... 0xFF (Apple's ROMAN.TXT)
const char16_t kMacRoman[128] = {
  0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
  0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
  0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
  0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
  0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
  0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
  0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
  0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7
};

} // namespace


std::string UTF8FromUTF16(const uint16_t * inChars, size_t inCount)
{
  std::string text;
  text.reserve(inCount);
  for (size_t i = 0; i < inCount; ++i) {
    char32_t c = inChars[i];
    if (c >= 0xD800 && c < 0xDC00) {
      if (i + 1 < inCount && inChars[i + 1] >= 0xDC00 && inChars[i + 1] < 0xE000) {
        c = 0x10000 + ((c - 0xD800) << 10) + (inChars[i + 1] - 0xDC00);
        ++i;
      } else {
        c = kReplacement;
      }
    } else if (c >= 0xDC00 && c < 0xE000) {
      c = kReplacement;
    }
    AppendUTF8(text, c);
  }
  return text;
}


std::string UTF8FromUTF16(const std::u16string & inText)
{
  return UTF8FromUTF16(reinterpret_cast<const uint16_t *>(inText.data()), inText.size());
}


std::u16string UTF16FromUTF8(const std::string & inText)
{
  std::u16string text;
  text.reserve(inText.size());
  const unsigned char * p = reinterpret_cast<const unsigned char *>(inText.data());
  size_t n = inText.size();
  for (size_t i = 0; i < n; ) {
    unsigned char b = p[i];
    char32_t c;
    size_t extra;
    if (b < 0x80)                { c = b; extra = 0; }
    else if ((b & 0xE0) == 0xC0) { c = b & 0x1F; extra = 1; }
    else if ((b & 0xF0) == 0xE0) { c = b & 0x0F; extra = 2; }
    else if ((b & 0xF8) == 0xF0) { c = b & 0x07; extra = 3; }
    else                         { text += char16_t(kReplacement); ++i; continue; }
    bool ok = true;
    for (size_t k = 1; ok && k <= extra; ++k) {
      if (i + k >= n || (p[i + k] & 0xC0) != 0x80)
        ok = false;
      else
        c = (c << 6) | (p[i + k] & 0x3F);
    }
    // overlong forms, surrogates, and beyond U+10FFFF aren't UTF-8
    static const char32_t kMin[4] = { 0, 0x80, 0x800, 0x10000 };
    if (!ok || c < kMin[extra] || (c >= 0xD800 && c < 0xE000) || c > 0x10FFFF) {
      text += char16_t(kReplacement);
      ++i;
      continue;
    }
    if (c >= 0x10000) {
      c -= 0x10000;
      text += char16_t(0xD800 + (c >> 10));
      text += char16_t(0xDC00 + (c & 0x3FF));
    } else {
      text += char16_t(c);
    }
    i += extra + 1;
  }
  return text;
}


std::string UTF8FromMacRoman(const char * inText, size_t inLength)
{
  std::string text;
  text.reserve(inLength);
  for (size_t i = 0; i < inLength; ++i) {
    unsigned char b = (unsigned char)inText[i];
    AppendUTF8(text, b < 0x80 ? char32_t(b) : char32_t(kMacRoman[b - 0x80]));
  }
  return text;
}


std::string UTF8FromMacRoman(const std::string & inText)
{
  return UTF8FromMacRoman(inText.data(), inText.size());
}


std::string ReplaceAll(const std::string & inText, const std::string & inFrom, const std::string & inTo)
{
  if (inFrom.empty())
    return inText;
  std::string text;
  size_t start = 0, found;
  while ((found = inText.find(inFrom, start)) != std::string::npos) {
    text.append(inText, start, found - start);
    text += inTo;
    start = found + inFrom.size();
  }
  text.append(inText, start, std::string::npos);
  return text;
}


std::string ReplaceControlCharacters(const std::string & inText, const std::string & inWith)
{
  std::string text;
  text.reserve(inText.size());
  for (size_t i = 0; i < inText.size(); ++i) {
    unsigned char b = (unsigned char)inText[i];
    if (b < 0x20 || b == 0x7F) {
      text += inWith;
    } else if (b == 0xC2 && i + 1 < inText.size()
               && (unsigned char)inText[i + 1] >= 0x80 && (unsigned char)inText[i + 1] <= 0x9F) {
      text += inWith;       // U+0080 ... U+009F
      ++i;
    } else {
      text += char(b);
    }
  }
  return text;
}


std::string NewtonLineEnds(const std::string & inText)
{
  std::string text;
  text.reserve(inText.size());
  for (size_t i = 0; i < inText.size(); ++i) {
    char c = inText[i];
    if (c == '\r' && i + 1 < inText.size() && inText[i + 1] == '\n')
      continue;               // CR LF: the LF makes the CR
    text += (c == '\n') ? '\r' : c;
  }
  return text;
}


std::string DesktopLineEnds(const std::string & inText)
{
  std::string text;
  text.reserve(inText.size());
  for (size_t i = 0; i < inText.size(); ++i) {
    char c = inText[i];
    if (c == '\r') {
      text += '\n';
      if (i + 1 < inText.size() && inText[i + 1] == '\n')
        ++i;
    } else {
      text += c;
    }
  }
  return text;
}

} // namespace ncx
