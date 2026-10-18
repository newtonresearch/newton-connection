/*
 File: StorePanel.cc

 The store panel (StorePanel.h).
 */

#include "ui/panels/StorePanel.h"
#include "ui/Format.h"
#include "ui/Images.h"

#include <FL/Fl_Box.H>
#include <FL/Fl_Progress.H>

namespace ncx {

std::string StoreStatusText(const StoreRecord & inStore)
{
  std::string text;
  if (inStore.defaultStore)
    text = "Default store";
  if (inStore.password && !inStore.password->empty())
    text = text.empty() ? std::string("Password protected.") : text + ", password protected.";
  if (inStore.readOnly)
    text = text.empty() ? std::string("Read only") : text + ", read only";
  return text.empty() ? std::string("--") : text;
}


StorePanel::StorePanel(int inX, int inY, int inW, int inH)
  : StorePanelUI(inX, inY, inW, inH)
{
  resizable(nullptr);       // the layout stays as it is (Fl_Group's would stretch it)
  box(FL_NO_BOX);
  mLevel->minimum(0);
  mLevel->maximum(100);
}


void StorePanel::update(const StoreRecord & inStore)
{
  mStoreBox->copy_label(inStore.name.c_str());
  mPicture->image(BuiltinImage(IsInternal(inStore) ? "store-internal" : "store-card"));
  mKind->copy_label(inStore.kind.c_str());
  mStatus->copy_label(StoreStatusText(inStore).c_str());
  mSignature->copy_label(std::to_string(inStore.signature).c_str());
  mCapacity->copy_label(SizeText(inStore.totalSize).c_str());
  mUsed->copy_label(SizeText(inStore.usedSize).c_str());
  mFree->copy_label(SizeText(inStore.totalSize - inStore.usedSize).c_str());
  double percent = inStore.totalSize > 0 ? 100.0 * inStore.usedSize / inStore.totalSize : 0;
  mLevel->value((float)percent);
  mLevel->selection_color(percent >= 90 ? FL_RED : percent >= 80 ? FL_YELLOW : FL_GREEN);
  std::string level = std::to_string((int)(percent + 0.5)) + " %";
  mLevel->copy_label(level.c_str());
  redraw();
}

} // namespace ncx
