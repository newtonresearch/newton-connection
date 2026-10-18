/*
 File: SoupPanel.h

 The soup panel (PORTING.md 7.4; the Cocoa app's NCSoupViewController): a
 soup's name and how many entries it has, and a table of them (SoupData.h:
 the columns per soup). A click on a column's header sorts by it (again:
 the other way); several rows can be selected (shift, the command key).
 The entries come from the thread with the Newton objects (App::
 objectWork): until then the panel says it is reading them.
 */

#ifndef NCX_UI_PANELS_SOUPPANEL_H
#define NCX_UI_PANELS_SOUPPANEL_H

#include "ui/SoupData.h"

#include <FL/Fl_Group.H>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

class Fl_Box;

namespace ncx {

class SoupTable;

class SoupPanel : public Fl_Group
{
public:
  SoupPanel(int inX, int inY, int inW, int inH);

  /** Show this soup of the file (read again if it is the one shown). */
  void load(const std::filesystem::path & inFile, int64_t inSoupPk);
  /** What is shown (tests): rows, columns, the selection. */
  const SoupData & data() const { return mData; }
  bool loading() const { return mLoading; }
  /** Select a row as the table shows them now (tests; a click). */
  void selectRow(int inRow, bool inSelected);
  /** The entries selected (their pks), in the table's order. */
  std::vector<int64_t> selectedEntries() const;
  /** Sort by a column (tests; a click on its header). */
  void sortBy(int inColumn, bool inAscending);
  /** The text of a cell as the table shows it now (tests). */
  std::string cellText(int inRow, int inColumn) const;

  /** Told when the selection changes. */
  void setSelectionChanged(std::function<void()> inChanged) { mSelectionChanged = std::move(inChanged); }

private:
  friend class SoupTable;
  void display(SoupData && inData);
  void updateHeader();

  Fl_Box * mHeader = nullptr;
  SoupTable * mTable = nullptr;
  SoupData mData;
  bool mLoading = false;
  std::filesystem::path mFile;
  int64_t mSoupPk = 0;
  unsigned mGeneration = 0;
  std::shared_ptr<bool> mAlive;
  std::function<void()> mSelectionChanged;
};

} // namespace ncx

#endif // NCX_UI_PANELS_SOUPPANEL_H
