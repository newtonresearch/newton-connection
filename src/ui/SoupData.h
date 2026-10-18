/*
 File: SoupData.h

 A soup's entries as the soup panel shows them (PORTING.md 7.4; the Cocoa
 app's NCSoupViewController columns): the _uniqueId, the soup's own columns
 (slot.plist: SoupColumns), the folder if the soup is filed, the time it
 was changed. Made from the entries' frames, so on the thread with the
 Newton objects (App::objectWork); the UI gets plain text. No FLTK and no
 framework headers here.
 */

#ifndef NCX_UI_SOUPDATA_H
#define NCX_UI_SOUPDATA_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ncx {

struct SoupData
{
  struct Column
  {
    std::string header;
    bool numeric = false;               // sorted by key, not by text
  };
  struct Row
  {
    int64_t entryPk = 0;
    int64_t uniqueId = 0;
    std::vector<std::string> cells;     // a text a column
    std::vector<double> keys;           // a sort key a column (numeric ones)
  };

  std::string soupName;
  std::vector<Column> columns;
  std::vector<Row> rows;                // by _uniqueId
  std::string error;                    // why there are none
};

/** The soup's entries in the file. Needs the Newton object system: run as
    object work. */
SoupData ReadSoupData(const std::filesystem::path & inFile, int64_t inSoupPk);

} // namespace ncx

#endif // NCX_UI_SOUPDATA_H
