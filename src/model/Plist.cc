/*
 File: Plist.cc

 Binary property lists (Plist.h). The format: "bplist00", the objects, a
 table of their offsets, and a 32-byte trailer (offset size, reference size,
 number of objects, the top object, where the table is). An object begins
 with a marker: its type in the high nibble, its size in the low one (0xF:
 an integer object with the size follows).
 */

#include "model/Plist.h"
#include "base/Strings.h"

#include <cstring>

namespace ncx {

/*-- making ------------------------------------------------------------------*/

Plist Plist::Bool(bool inValue) { Plist p; p.mType = kBool; p.mInt = inValue ? 1 : 0; return p; }
Plist Plist::Int(int64_t inValue) { Plist p; p.mType = kInt; p.mInt = inValue; return p; }
Plist Plist::Real(double inValue) { Plist p; p.mType = kReal; p.mReal = inValue; return p; }
Plist Plist::Date(double inSeconds) { Plist p; p.mType = kDate; p.mReal = inSeconds; return p; }
Plist Plist::Data(std::vector<uint8_t> inBytes) { Plist p; p.mType = kData; p.mData = std::move(inBytes); return p; }
Plist Plist::String(const std::string & inUTF8) { Plist p; p.mType = kString; p.mString = inUTF8; return p; }
Plist Plist::Array(std::vector<Plist> inItems) { Plist p; p.mType = kArray; p.mItems = std::move(inItems); return p; }
Plist Plist::Dict() { Plist p; p.mType = kDict; return p; }
Plist Plist::UID(uint64_t inValue) { Plist p; p.mType = kUID; p.mInt = (int64_t)inValue; return p; }


const Plist * Plist::find(const std::string & inKey) const
{
  for (size_t i = 0; i < mKeys.size(); ++i)
    if (mKeys[i] == inKey)
      return &mItems[i];
  return nullptr;
}


void Plist::set(const std::string & inKey, Plist inValue)
{
  for (size_t i = 0; i < mKeys.size(); ++i)
    if (mKeys[i] == inKey) {
      mItems[i] = std::move(inValue);
      return;
    }
  mKeys.push_back(inKey);
  mItems.push_back(std::move(inValue));
}


bool Plist::operator==(const Plist & inOther) const
{
  if (mType == kDict && inOther.mType == kDict) {
    // keys in any order (Core Data writes them as they hash)
    if (mKeys.size() != inOther.mKeys.size())
      return false;
    for (size_t i = 0; i < mKeys.size(); ++i) {
      const Plist * other = inOther.find(mKeys[i]);
      if (!other || !(*other == mItems[i]))
        return false;
    }
    return true;
  }
  return mType == inOther.mType && mInt == inOther.mInt && mReal == inOther.mReal && mData == inOther.mData
      && mString == inOther.mString && mKeys == inOther.mKeys && mItems == inOther.mItems;
}


/*-- reading -----------------------------------------------------------------*/

namespace {

uint64_t BigEndian(const uint8_t * inBytes, size_t inSize)
{
  uint64_t value = 0;
  for (size_t i = 0; i < inSize; ++i)
    value = value << 8 | inBytes[i];
  return value;
}

class Reader
{
public:
  Reader(const std::vector<uint8_t> & inBytes) : mBytes(inBytes) { }

  bool read(Plist & outPlist, std::string & outError)
  {
    if (mBytes.size() < 8 + 32 || std::memcmp(mBytes.data(), "bplist00", 8) != 0) {
      outError = "not a binary plist";
      return false;
    }
    const uint8_t * trailer = mBytes.data() + mBytes.size() - 32;
    mOffsetSize = trailer[6];
    mRefSize = trailer[7];
    mCount = BigEndian(trailer + 8, 8);
    uint64_t top = BigEndian(trailer + 16, 8);
    mTable = BigEndian(trailer + 24, 8);
    if (mOffsetSize < 1 || mOffsetSize > 8 || mRefSize < 1 || mRefSize > 8 || top >= mCount
     || mTable + mCount * mOffsetSize > mBytes.size() - 32) {
      outError = "a binary plist's trailer is broken";
      return false;
    }
    if (!object(top, outPlist, 0)) {
      outError = "a binary plist's object is broken";
      return false;
    }
    return true;
  }

private:
  bool offsetOf(uint64_t inObject, uint64_t & outOffset)
  {
    if (inObject >= mCount)
      return false;
    outOffset = BigEndian(mBytes.data() + mTable + inObject * mOffsetSize, mOffsetSize);
    return outOffset >= 8 && outOffset < mTable;
  }

  // the size in a marker's low nibble, or the integer after it
  bool length(uint64_t & ioOffset, uint8_t inNibble, uint64_t & outLength)
  {
    if (inNibble != 0xF) {
      outLength = inNibble;
      return true;
    }
    if (ioOffset >= mTable)
      return false;
    uint8_t marker = mBytes[ioOffset++];
    if ((marker & 0xF0) != 0x10)
      return false;
    size_t size = (size_t)1 << (marker & 0x0F);
    if (size > 8 || ioOffset + size > mTable)
      return false;
    outLength = BigEndian(mBytes.data() + ioOffset, size);
    ioOffset += size;
    return true;
  }

  bool object(uint64_t inObject, Plist & outPlist, int inDepth)
  {
    uint64_t offset;
    if (inDepth > 64 || !offsetOf(inObject, offset))
      return false;
    uint8_t marker = mBytes[offset++];
    uint8_t high = marker >> 4, low = marker & 0x0F;
    uint64_t n = 0;
    switch (high) {
      case 0x0:
        if (marker == 0x00) { outPlist = Plist(); return true; }
        if (marker == 0x08 || marker == 0x09) { outPlist = Plist::Bool(marker == 0x09); return true; }
        return false;
      case 0x1: {
        size_t size = (size_t)1 << low;
        if (size > 16 || offset + size > mTable)
          return false;
        // 1, 2, 4 bytes unsigned; 8 signed; 16: the low 8 bytes
        uint64_t value = BigEndian(mBytes.data() + offset + (size == 16 ? 8 : 0), size == 16 ? 8 : size);
        outPlist = Plist::Int((int64_t)value);
        return true;
      }
      case 0x2: case 0x3: {
        size_t size = high == 0x3 ? 8 : (size_t)1 << low;
        if ((size != 4 && size != 8) || offset + size > mTable)
          return false;
        uint64_t bits = BigEndian(mBytes.data() + offset, size);
        double value;
        if (size == 4) {
          uint32_t b = (uint32_t)bits;
          float f;
          std::memcpy(&f, &b, 4);
          value = f;
        } else {
          std::memcpy(&value, &bits, 8);
        }
        outPlist = high == 0x3 ? Plist::Date(value) : Plist::Real(value);
        return true;
      }
      case 0x4:
        if (!length(offset, low, n) || offset + n > mTable)
          return false;
        outPlist = Plist::Data(std::vector<uint8_t>(mBytes.begin() + (long)offset, mBytes.begin() + (long)(offset + n)));
        return true;
      case 0x5:
        if (!length(offset, low, n) || offset + n > mTable)
          return false;
        outPlist = Plist::String(UTF8FromMacRoman((const char *)mBytes.data() + offset, (size_t)n));   // ASCII
        return true;
      case 0x6: {
        if (!length(offset, low, n) || offset + n * 2 > mTable)
          return false;
        std::vector<uint16_t> chars((size_t)n);
        for (uint64_t i = 0; i < n; ++i)
          chars[(size_t)i] = (uint16_t)BigEndian(mBytes.data() + offset + i * 2, 2);
        outPlist = Plist::String(UTF8FromUTF16(chars.data(), chars.size()));
        return true;
      }
      case 0x8:
        if (offset + low + 1 > mTable)
          return false;
        outPlist = Plist::UID(BigEndian(mBytes.data() + offset, (size_t)low + 1));
        return true;
      case 0xA: case 0xD: {
        if (!length(offset, low, n))
          return false;
        uint64_t refs = high == 0xD ? n * 2 : n;
        if (offset + refs * mRefSize > mTable)
          return false;
        if (high == 0xA) {
          outPlist = Plist::Array();
          for (uint64_t i = 0; i < n; ++i) {
            Plist item;
            if (!object(BigEndian(mBytes.data() + offset + i * mRefSize, mRefSize), item, inDepth + 1))
              return false;
            outPlist.append(std::move(item));
          }
        } else {
          outPlist = Plist::Dict();
          for (uint64_t i = 0; i < n; ++i) {
            Plist key, value;
            if (!object(BigEndian(mBytes.data() + offset + i * mRefSize, mRefSize), key, inDepth + 1)
             || !object(BigEndian(mBytes.data() + offset + (n + i) * mRefSize, mRefSize), value, inDepth + 1)
             || key.type() != Plist::kString)
              return false;
            outPlist.set(key.asString(), std::move(value));
          }
        }
        return true;
      }
      default:
        return false;
    }
  }

  const std::vector<uint8_t> & mBytes;
  size_t mOffsetSize = 0, mRefSize = 0;
  uint64_t mCount = 0, mTable = 0;
};


/*-- writing -----------------------------------------------------------------*/

class Writer
{
public:
  std::vector<uint8_t> write(const Plist & inTop)
  {
    count(inTop);
    mRefSize = mTotal < 256 ? 1 : (mTotal < 65536 ? 2 : 4);
    mBytes.assign({ 'b', 'p', 'l', 'i', 's', 't', '0', '0' });
    mNext = 0;
    object(inTop);
    uint64_t table = mBytes.size();
    uint64_t largest = mOffsets.empty() ? 0 : mOffsets.back();
    size_t offsetSize = largest < 256 ? 1 : (largest < 65536 ? 2 : (largest < 0x100000000ULL ? 4 : 8));
    for (uint64_t offset : mOffsets)
      put(offset, offsetSize);
    uint8_t trailer[32] = {};
    trailer[6] = (uint8_t)offsetSize;
    trailer[7] = (uint8_t)mRefSize;
    for (int i = 0; i < 8; ++i) {
      trailer[8 + i] = (uint8_t)(mOffsets.size() >> (56 - 8 * i));
      trailer[16 + i] = 0;                                      // the top object is the first
      trailer[24 + i] = (uint8_t)(table >> (56 - 8 * i));
    }
    mBytes.insert(mBytes.end(), trailer, trailer + 32);
    return mBytes;
  }

private:
  // objects get numbers depth first: an object, then its children
  void count(const Plist & inPlist)
  {
    ++mTotal;
    if (inPlist.type() == Plist::kDict)
      mTotal += inPlist.size();         // the keys
    if (inPlist.type() == Plist::kArray || inPlist.type() == Plist::kDict)
      for (size_t i = 0; i < inPlist.size(); ++i)
        count(inPlist.type() == Plist::kDict ? inPlist.valueAt(i) : inPlist.at(i));
  }

  void put(uint64_t inValue, size_t inSize)
  {
    for (size_t i = inSize; i > 0; --i)
      mBytes.push_back((uint8_t)(inValue >> (8 * (i - 1))));
  }

  void marker(uint8_t inHigh, uint64_t inLength)
  {
    if (inLength < 15) {
      mBytes.push_back((uint8_t)(inHigh << 4 | inLength));
    } else {
      mBytes.push_back((uint8_t)(inHigh << 4 | 0xF));
      integer((int64_t)inLength);
    }
  }

  void integer(int64_t inValue)
  {
    uint64_t v = (uint64_t)inValue;
    if (inValue >= 0 && v < 0x100) { mBytes.push_back(0x10); put(v, 1); }
    else if (inValue >= 0 && v < 0x10000) { mBytes.push_back(0x11); put(v, 2); }
    else if (inValue >= 0 && v < 0x100000000ULL) { mBytes.push_back(0x12); put(v, 4); }
    else { mBytes.push_back(0x13); put(v, 8); }
  }

  // writes inPlist (number mNext) and its children; returns its number
  uint64_t object(const Plist & inPlist)
  {
    uint64_t number = mNext++;
    if (mOffsets.size() <= number)
      mOffsets.resize(number + 1);
    switch (inPlist.type()) {
      case Plist::kArray: {
        std::vector<uint64_t> refs(inPlist.size());
        size_t at = reserve(number, 0xA, inPlist.size(), inPlist.size());
        for (size_t i = 0; i < inPlist.size(); ++i)
          refs[i] = object(inPlist.at(i));
        fill(at, refs);
        return number;
      }
      case Plist::kDict: {
        std::vector<uint64_t> refs(inPlist.size() * 2);
        size_t at = reserve(number, 0xD, inPlist.size(), inPlist.size() * 2);
        for (size_t i = 0; i < inPlist.size(); ++i)
          refs[i] = object(Plist::String(inPlist.keyAt(i)));
        for (size_t i = 0; i < inPlist.size(); ++i)
          refs[inPlist.size() + i] = object(inPlist.valueAt(i));
        fill(at, refs);
        return number;
      }
      default:
        mOffsets[number] = mBytes.size();
        scalar(inPlist);
        return number;
    }
  }

  // a container's marker and room for its references (filled in later)
  size_t reserve(uint64_t inNumber, uint8_t inHigh, size_t inLength, size_t inRefs)
  {
    mOffsets[inNumber] = mBytes.size();
    marker(inHigh, inLength);
    size_t at = mBytes.size();
    mBytes.resize(mBytes.size() + inRefs * mRefSize);
    return at;
  }

  void fill(size_t inAt, const std::vector<uint64_t> & inRefs)
  {
    for (size_t i = 0; i < inRefs.size(); ++i)
      for (size_t k = 0; k < mRefSize; ++k)
        mBytes[inAt + i * mRefSize + k] = (uint8_t)(inRefs[i] >> (8 * (mRefSize - 1 - k)));
  }

  void scalar(const Plist & inPlist)
  {
    switch (inPlist.type()) {
      case Plist::kNull: mBytes.push_back(0x00); break;
      case Plist::kBool: mBytes.push_back(inPlist.asBool() ? 0x09 : 0x08); break;
      case Plist::kInt: integer(inPlist.asInt()); break;
      case Plist::kReal: case Plist::kDate: {
        mBytes.push_back(inPlist.type() == Plist::kDate ? 0x33 : 0x23);
        double value = inPlist.asReal();
        uint64_t bits;
        std::memcpy(&bits, &value, 8);
        put(bits, 8);
        break;
      }
      case Plist::kData:
        marker(0x4, inPlist.asData().size());
        mBytes.insert(mBytes.end(), inPlist.asData().begin(), inPlist.asData().end());
        break;
      case Plist::kString: {
        const std::string & s = inPlist.asString();
        bool ascii = true;
        for (unsigned char c : s)
          ascii = ascii && c < 0x80;
        if (ascii) {
          marker(0x5, s.size());
          mBytes.insert(mBytes.end(), s.begin(), s.end());
        } else {
          std::u16string chars = UTF16FromUTF8(s);
          marker(0x6, chars.size());
          for (char16_t c : chars)
            put(c, 2);
        }
        break;
      }
      case Plist::kUID: {
        uint64_t v = (uint64_t)inPlist.asInt();
        size_t size = v < 0x100 ? 1 : (v < 0x10000 ? 2 : (v < 0x100000000ULL ? 4 : 8));
        mBytes.push_back((uint8_t)(0x80 | (size - 1)));
        put(v, size);
        break;
      }
      default:
        break;
    }
  }

  std::vector<uint8_t> mBytes;
  std::vector<uint64_t> mOffsets;
  size_t mTotal = 0, mRefSize = 1;
  uint64_t mNext = 0;
};

} // namespace


bool Plist::Read(const std::vector<uint8_t> & inBytes, Plist & outPlist, std::string & outError)
{
  return Reader(inBytes).read(outPlist, outError);
}


std::vector<uint8_t> Plist::write() const
{
  return Writer().write(*this);
}

} // namespace ncx
