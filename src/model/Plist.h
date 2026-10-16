/*
 File: Plist.h

 Apple's binary property lists (bplist00), as far as the .newtondevice
 files need them: Core Data keeps a store's metadata in one (Z_METADATA:
 the model's hashes, and NCX's NewtonId and NewtonName). Reads and writes
 null, booleans, integers, reals, dates, data, strings (ASCII and UTF-16),
 arrays, dictionaries, and UIDs (NSKeyedArchiver's references). Strings are
 UTF-8 here.
 */

#ifndef NCX_MODEL_PLIST_H
#define NCX_MODEL_PLIST_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ncx {

class Plist
{
public:
  enum Type { kNull, kBool, kInt, kReal, kDate, kData, kString, kArray, kDict, kUID };

  Plist() { }
  static Plist Bool(bool inValue);
  static Plist Int(int64_t inValue);
  static Plist Real(double inValue);
  static Plist Date(double inSecondsSince2001);
  static Plist Data(std::vector<uint8_t> inBytes);
  static Plist String(const std::string & inUTF8);
  static Plist Array(std::vector<Plist> inItems = {});
  static Plist Dict();
  static Plist UID(uint64_t inValue);

  Type type() const { return mType; }
  bool asBool() const { return mInt != 0; }
  int64_t asInt() const { return mInt; }
  double asReal() const { return mReal; }
  const std::vector<uint8_t> & asData() const { return mData; }
  const std::string & asString() const { return mString; }

  /** Arrays */
  size_t size() const { return mType == kDict ? mKeys.size() : mItems.size(); }
  const Plist & at(size_t inIndex) const { return mItems[inIndex]; }
  void append(Plist inItem) { mItems.push_back(std::move(inItem)); }

  /** Dictionaries: in the order the keys came. find: nullptr if there is no
      such key. set: replaces, or adds at the end. */
  const Plist * find(const std::string & inKey) const;
  void set(const std::string & inKey, Plist inValue);
  const std::string & keyAt(size_t inIndex) const { return mKeys[inIndex]; }
  const Plist & valueAt(size_t inIndex) const { return mItems[inIndex]; }

  /** A binary plist's bytes; false (and why) if they aren't one. */
  static bool Read(const std::vector<uint8_t> & inBytes, Plist & outPlist, std::string & outError);
  std::vector<uint8_t> write() const;

  bool operator==(const Plist & inOther) const;

private:
  Type mType = kNull;
  int64_t mInt = 0;
  double mReal = 0;
  std::vector<uint8_t> mData;
  std::string mString;
  std::vector<std::string> mKeys;       // dictionaries
  std::vector<Plist> mItems;            // arrays, dictionaries' values
};

} // namespace ncx

#endif // NCX_MODEL_PLIST_H
