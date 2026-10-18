/*
 File: Inspector.cc

 The inspector (Inspector.h).
 */

#include "ui/Inspector.h"
#include "ui/App.h"
#include "ui/EntryPreview.h"
#include "ui/PackageInfo.h"
#include "ui/panels/PackageView.h"
#include "ui/panels/RichView.h"

#include "base/Threads.h"

#include <FL/Fl_Box.H>

#include <cstdint>

namespace ncx {

Inspector::Inspector(int inX, int inY, int inW, int inH)
  : Fl_Group(inX, inY, inW, inH), mAlive(std::make_shared<bool>(true))
{
  box(FL_FLAT_BOX);
  color(FL_BACKGROUND2_COLOR);
  mStatus = new Fl_Box(inX, inY, inW, inH);
  mStatus->labelcolor(fl_inactive(FL_FOREGROUND_COLOR));
  mStatus->align(FL_ALIGN_INSIDE | FL_ALIGN_CENTER | FL_ALIGN_WRAP);
  mPackage = new PackageView(inX, inY, inW, inH);
  mPackage->hide();
  mPreview = new RichView(inX, inY, inW, inH);
  mPreview->hide();
  resizable(mStatus);
  end();
  notApplicable();
}


void Inspector::showStatus(const std::string & inText)
{
  mLoading = false;
  ++mGeneration;
  mPackage->hide();
  mPreview->hide();
  mStatus->copy_label(inText.c_str());
  mStatus->show();
  redraw();
}


void Inspector::notApplicable()
{
  mFile.clear();
  mEntries.clear();
  showStatus("Inspector not applicable");
}


void Inspector::inspect(const std::filesystem::path & inFile, const std::string & inSoupName, const std::string & inAppName,
                        const std::vector<int64_t> & inEntryPks)
{
  if (inFile == mFile && inEntryPks == mEntries && (mLoading || !mStatus->visible()))
    return;       // showing it, or about to
  mFile = inFile;
  mEntries = inEntryPks;
  if (inEntryPks.empty()) {
    showStatus("No selection");
    return;
  }
  if (inEntryPks.size() > 1) {
    showStatus("Multiple selection");
    return;
  }
  showStatus("Reading...");
  mLoading = true;
  unsigned generation = mGeneration;
  std::weak_ptr<bool> alive = mAlive;
  int64_t entry = inEntryPks[0];
  if (inSoupName != "Packages") {
    // as its translator exports it, into a folder of the request's own
    std::filesystem::path folder = std::filesystem::temp_directory_path()
                                 / ("ncx-preview-" + std::to_string((uintptr_t)this) + "-" + std::to_string(generation));
    App::get().objectWork([this, alive, generation, inFile, entry, inAppName, folder]() {
      auto preview = std::make_shared<EntryPreview>(ReadEntryPreview(inFile, entry, inAppName, folder));
      RunOnMainThread([this, alive, generation, preview]() {
        if (alive.expired() || generation != mGeneration)
          return;
        mLoading = false;
        if (!preview->error.empty()) {
          showStatus("Can't show the entry: " + preview->error);
          return;
        }
        mStatus->hide();
        mPreview->display(*preview);
        mPreview->show();
        redraw();
      });
    });
    return;
  }
  App::get().objectWork([this, alive, generation, inFile, entry]() {
    auto info = std::make_shared<PackageInfo>(ReadPackageEntryInfo(inFile, entry));
    RunOnMainThread([this, alive, generation, info]() {
      if (alive.expired() || generation != mGeneration)
        return;
      mLoading = false;
      mStatus->hide();
      mPackage->display(*info);
      mPackage->show();
      redraw();
    });
  });
}


std::string Inspector::status() const
{
  return mStatus->visible() && mStatus->label() ? mStatus->label() : "";
}

} // namespace ncx
