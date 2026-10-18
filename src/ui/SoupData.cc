/*
 File: SoupData.cc

 A soup's entries as the soup panel shows them (SoupData.h).
 */

#include "ui/SoupData.h"

#include "base/Dates.h"
#include "base/NewtonCall.h"
#include "base/NewtonStrings.h"
#include "model/NewtonModel.h"

#include "RSSymbols.h"

#include <exception>

namespace ncx {

SoupData ReadSoupData(const std::filesystem::path & inFile, int64_t inSoupPk)
{
  SoupData data;
  std::string error;
  std::unique_ptr<DeviceFile> file = DeviceFile::Open(inFile, DeviceFile::kReadOnly, error);
  if (!file) {
    data.error = error;
    return data;
  }
  try {
    std::optional<SoupRecord> soup = file->soup(inSoupPk);
    if (!soup) {
      data.error = "the soup is gone";
      return data;
    }
    data.soupName = soup->name;
    const std::vector<SoupColumn> & columns = SoupColumns(soup->name);
    bool labels = SoupShowsLabels(soup->name);
    data.columns.push_back({ "ID", true });
    for (const SoupColumn & column : columns)
      data.columns.push_back({ column.header, column.type == "dateRef" || column.type == "date" });
    if (labels)
      data.columns.push_back({ "Folder", false });
    data.columns.push_back({ "Modified", true });

    for (const EntryRecord & entry : file->entries(soup->pk, true)) {
      SoupData::Row row;
      row.entryPk = entry.pk;
      row.uniqueId = entry.uniqueId;
      row.cells.push_back(std::to_string(entry.uniqueId));
      row.keys.push_back((double)entry.uniqueId);
      CallNewton([&] {
        RefVar frame(EntryFrame(entry));
        for (const SoupColumn & column : columns) {
          std::string text;
          double key = 0;
          if (column.tag == "title" && entry.title)
            text = *entry.title;              // as the backup made it (EntryTitle)
          else if (IsFrame(frame)) {
            RefVar slot(GetFrameSlot(frame, MakeSymbol(column.slot.c_str())));
            text = SlotText(slot, column.type);
            if ((column.type == "dateRef" || column.type == "date") && ISINT(slot))
              key = (double)TimeFromNewton(RINT(slot));
          }
          row.cells.push_back(text);
          row.keys.push_back(key);
        }
        if (labels) {
          RefVar folder(IsFrame(frame) ? GetFrameSlot(frame, SYMA(labels)) : (Ref)NILREF);
          // the folder's tag (the user's folder names are in the file's
          // UserInfo, a keyed archive the Cocoa app made)
          row.cells.push_back(IsSymbol(folder) ? UTF8FromSymbol(folder) : std::string());
          row.keys.push_back(0);
        }
      });
      int64_t modified = TimeFromCoreData(entry.modTime);
      row.cells.push_back(entry.modTime != 0 ? FormatDateTime(modified) : std::string());
      row.keys.push_back((double)modified);
      data.rows.push_back(std::move(row));
    }
  } catch (const std::exception & e) {
    data.error = e.what();
  }
  return data;
}

} // namespace ncx
