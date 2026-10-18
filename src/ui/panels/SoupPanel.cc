/*
 File: SoupPanel.cc

 The soup panel (SoupPanel.h).
 */

#include "ui/panels/SoupPanel.h"
#include "ui/App.h"
#include "ui/Format.h"

#include "base/Threads.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Table_Row.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <numeric>

namespace ncx {

namespace {

const int kHeaderHeight = 32;
const int kRowHeight = 20;

/** Text compared as people read it: case left out. */
int CompareText(const std::string & inA, const std::string & inB)
{
  size_t n = std::min(inA.size(), inB.size());
  for (size_t i = 0; i < n; ++i) {
    int a = std::tolower((unsigned char)inA[i]), b = std::tolower((unsigned char)inB[i]);
    if (a != b)
      return a < b ? -1 : 1;
  }
  return inA.size() < inB.size() ? -1 : (inA.size() > inB.size() ? 1 : 0);
}

} // namespace


/** The table: the data's rows in mOrder's order. */
class SoupTable : public Fl_Table_Row
{
public:
  SoupTable(SoupPanel & inPanel, int inX, int inY, int inW, int inH)
    : Fl_Table_Row(inX, inY, inW, inH), mPanel(inPanel)
  {
    type(SELECT_MULTI);
    col_header(1);
    col_header_height(kRowHeight + 2);
    col_resize(1);
    row_height_all(kRowHeight);
    selection_color(FL_SELECTION_COLOR);
    color(FL_BACKGROUND2_COLOR);
    when(FL_WHEN_CHANGED | FL_WHEN_RELEASE);
    callback(TableCB, this);
    end();
  }

  /** New data: all of it, by _uniqueId, nothing selected. */
  void reset()
  {
    const SoupData & data = mPanel.mData;
    mOrder.resize(data.rows.size());
    std::iota(mOrder.begin(), mOrder.end(), 0);
    mSortColumn = 0;
    mAscending = true;
    clear();
    cols((int)data.columns.size());
    rows((int)data.rows.size());
    row_height_all(kRowHeight);
    // ID and Modified narrow, the others share the rest
    int fixed = 0, flexible = 0;
    for (size_t c = 0; c < data.columns.size(); ++c)
      if (data.columns[c].numeric)
        fixed += c == 0 ? 70 : 150;
      else
        ++flexible;
    int rest = std::max(120, (tiw - fixed) / std::max(1, flexible));
    for (size_t c = 0; c < data.columns.size(); ++c)
      col_width((int)c, data.columns[c].numeric ? (c == 0 ? 70 : 150) : rest);
    select_all_rows(0);
    redraw();
  }

  void sortBy(int inColumn, bool inAscending)
  {
    const SoupData & data = mPanel.mData;
    if (inColumn < 0 || inColumn >= (int)data.columns.size())
      return;
    // keep the selection with its entries
    std::vector<bool> selected(data.rows.size(), false);
    for (int r = 0; r < rows(); ++r)
      if (row_selected(r))
        selected[mOrder[r]] = true;
    bool numeric = data.columns[inColumn].numeric;
    std::stable_sort(mOrder.begin(), mOrder.end(), [&](int inA, int inB) {
      const SoupData::Row & a = data.rows[inA], & b = data.rows[inB];
      int order = numeric ? (a.keys[inColumn] < b.keys[inColumn] ? -1 : (a.keys[inColumn] > b.keys[inColumn] ? 1 : 0))
                          : CompareText(a.cells[inColumn], b.cells[inColumn]);
      return inAscending ? order < 0 : order > 0;
    });
    mSortColumn = inColumn;
    mAscending = inAscending;
    for (int r = 0; r < rows(); ++r)
      select_row(r, selected[mOrder[r]] ? 1 : 0);
    redraw();
  }

  int dataRow(int inRow) const { return inRow >= 0 && inRow < (int)mOrder.size() ? mOrder[inRow] : -1; }

protected:
  void draw_cell(TableContext inContext, int inRow, int inCol, int inX, int inY, int inW, int inH) override
  {
    const SoupData & data = mPanel.mData;
    switch (inContext) {
      case CONTEXT_STARTPAGE:
        fl_font(FL_HELVETICA, 12);
        break;
      case CONTEXT_COL_HEADER: {
        fl_push_clip(inX, inY, inW, inH);
        fl_draw_box(FL_THIN_UP_BOX, inX, inY, inW, inH, FL_BACKGROUND_COLOR);
        fl_color(FL_FOREGROUND_COLOR);
        fl_font(FL_HELVETICA_BOLD, 12);
        if (inCol < (int)data.columns.size())
          fl_draw(data.columns[inCol].header.c_str(), inX + 4, inY, inW - 18, inH, FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        if (inCol == mSortColumn) {
          // the order: a small triangle
          int cx = inX + inW - 10, cy = inY + inH / 2;
          if (mAscending)
            fl_polygon(cx - 4, cy + 2, cx + 4, cy + 2, cx, cy - 3);
          else
            fl_polygon(cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
        }
        fl_font(FL_HELVETICA, 12);
        fl_pop_clip();
        break;
      }
      case CONTEXT_CELL: {
        int row = dataRow(inRow);
        if (row < 0 || inCol >= (int)data.rows[row].cells.size())
          break;
        fl_push_clip(inX, inY, inW, inH);
        bool selected = row_selected(inRow);
        Fl_Color background = selected ? selection_color() : ((inRow & 1) ? fl_color_average(FL_BACKGROUND2_COLOR, FL_BACKGROUND_COLOR, 0.85f) : FL_BACKGROUND2_COLOR);
        fl_color(background);
        fl_rectf(inX, inY, inW, inH);
        fl_color(selected ? fl_contrast(FL_FOREGROUND_COLOR, background) : FL_FOREGROUND_COLOR);
        fl_draw(data.rows[row].cells[inCol].c_str(), inX + 4, inY, inW - 8, inH,
                (data.columns[inCol].numeric && inCol == 0 ? FL_ALIGN_RIGHT : FL_ALIGN_LEFT) | FL_ALIGN_CLIP);
        fl_pop_clip();
        break;
      }
      default:
        break;
    }
  }

private:
  static void TableCB(Fl_Widget *, void * inTable)
  {
    SoupTable * table = static_cast<SoupTable *>(inTable);
    if (table->callback_context() == CONTEXT_COL_HEADER) {
      if (Fl::event() == FL_RELEASE && Fl::event_button() == FL_LEFT_MOUSE) {
        int column = table->callback_col();
        table->sortBy(column, column == table->mSortColumn ? !table->mAscending : true);
      }
      return;
    }
    if (table->callback_context() == CONTEXT_CELL && table->mPanel.mSelectionChanged)
      table->mPanel.mSelectionChanged();
  }

  SoupPanel & mPanel;
  std::vector<int> mOrder;
  int mSortColumn = 0;
  bool mAscending = true;
};


SoupPanel::SoupPanel(int inX, int inY, int inW, int inH)
  : Fl_Group(inX, inY, inW, inH), mAlive(std::make_shared<bool>(true))
{
  box(FL_FLAT_BOX);
  color(FL_BACKGROUND2_COLOR);
  mHeader = new Fl_Box(inX + 12, inY, inW - 24, kHeaderHeight);
  mHeader->align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT | FL_ALIGN_CLIP);
  mHeader->labelfont(FL_HELVETICA_BOLD);
  mHeader->labelsize(13);
  mTable = new SoupTable(*this, inX, inY + kHeaderHeight, inW, inH - kHeaderHeight);
  resizable(mTable);
  end();
}


void SoupPanel::load(const std::filesystem::path & inFile, int64_t inSoupPk)
{
  mFile = inFile;
  mSoupPk = inSoupPk;
  mLoading = true;
  unsigned generation = ++mGeneration;
  updateHeader();
  std::weak_ptr<bool> alive = mAlive;
  App::get().objectWork([this, alive, generation, inFile, inSoupPk]() {
    SoupData data = ReadSoupData(inFile, inSoupPk);
    auto shared = std::make_shared<SoupData>(std::move(data));
    RunOnMainThread([this, alive, generation, shared]() {
      // the panel may be gone, or showing another soup by now
      if (alive.expired() || generation != mGeneration)
        return;
      display(std::move(*shared));
    });
  });
}


void SoupPanel::display(SoupData && inData)
{
  mData = std::move(inData);
  mLoading = false;
  mTable->reset();
  updateHeader();
  if (mSelectionChanged)
    mSelectionChanged();
}


void SoupPanel::updateHeader()
{
  std::string text;
  if (mLoading)
    text = mData.soupName.empty() ? std::string("Reading the entries...") : mData.soupName + ": reading the entries...";
  else if (!mData.error.empty())
    text = "Can't read the entries: " + mData.error;
  else
    text = mData.soupName + ": " + GroupedNumber((int64_t)mData.rows.size())
         + (mData.rows.size() == 1 ? " entry" : " entries");
  std::string label;
  for (char c : text) {
    label += c;
    if (c == '@')
      label += '@';
  }
  mHeader->copy_label(label.c_str());
  redraw();
}


std::vector<int64_t> SoupPanel::selectedEntries() const
{
  std::vector<int64_t> pks;
  for (int r = 0; r < mTable->rows(); ++r)
    if (mTable->row_selected(r)) {
      int row = mTable->dataRow(r);
      if (row >= 0)
        pks.push_back(mData.rows[row].entryPk);
    }
  return pks;
}


void SoupPanel::selectAll()
{
  mTable->select_all_rows(1);
  if (mSelectionChanged)
    mSelectionChanged();
}


void SoupPanel::selectRow(int inRow, bool inSelected)
{
  mTable->select_row(inRow, inSelected ? 1 : 0);
  if (mSelectionChanged)
    mSelectionChanged();
}


void SoupPanel::sortBy(int inColumn, bool inAscending)
{
  mTable->sortBy(inColumn, inAscending);
}


std::string SoupPanel::cellText(int inRow, int inColumn) const
{
  int row = mTable->dataRow(inRow);
  if (row < 0 || inColumn < 0 || inColumn >= (int)mData.rows[row].cells.size())
    return std::string();
  return mData.rows[row].cells[inColumn];
}

} // namespace ncx
