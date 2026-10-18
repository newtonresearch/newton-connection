/*
 File: PackageInfo.h

 What a package says of itself (PORTING.md 7.4; the Cocoa app's PkgInfo,
 PkgPart, PkgFormPart, PkgBookPart): its directory (name, id, version,
 copyright, size, when it was made, copy protection) and its parts (type,
 size; a form's text and icon; a book's title, ISBN, author, copyright,
 publication date). Read with the Newton object system (the parts' frames,
 the icons drawn): run as object work. No FLTK and no framework headers
 here.
 */

#ifndef NCX_UI_PACKAGEINFO_H
#define NCX_UI_PACKAGEINFO_H

#include "translators/Canvas.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ncx {

struct PackagePartInfo
{
  std::string title;            // "Part 0  (form)"
  std::string size;             // "12,345 bytes"
  bool form = false;            // a form ('form', 'auto'): text, icon
  std::string text;
  Pixels icon;
  bool book = false;            // a book ('book')
  std::string bookTitle, isbn, author, copyright, published;
};

struct PackageInfo
{
  std::string name, ident, version, copyright, size, created;
  bool copyProtected = false;
  std::vector<PackagePartInfo> parts;
  std::string error;            // why there is nothing
};

/** A package's info from its bytes (a .newtonpkg file's, a pkgRef's). */
PackageInfo ReadPackageInfo(const std::vector<uint8_t> & inPackage);
/** ... from a package entry in a backup file (its pkgRef). */
PackageInfo ReadPackageEntryInfo(const std::filesystem::path & inFile, int64_t inEntryPk);

} // namespace ncx

#endif // NCX_UI_PACKAGEINFO_H
