/*
 File: Inspector.h

 The inspector (PORTING.md 7.4, 7.5; the Cocoa app's
 NCInspectorViewController): what the soup panel has selected. "No
 selection", "Multiple selection", "Inspector not applicable" (nothing to
 inspect where the sidebar is); a package entry's info (PackageView); an
 entry as its translator exports it (RichView: EntryPreview.h). What it shows is read as object work, as the soup panel's
 entries are.
 */

#ifndef NCX_UI_INSPECTOR_H
#define NCX_UI_INSPECTOR_H

#include <FL/Fl_Group.H>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class Fl_Box;

namespace ncx {

class PackageView;
class RichView;

class Inspector : public Fl_Group
{
public:
  Inspector(int inX, int inY, int inW, int inH);

  /** Nothing to inspect here. */
  void notApplicable();
  /** The soup's entries selected in the file. */
  void inspect(const std::filesystem::path & inFile, const std::string & inSoupName, const std::string & inAppName,
               const std::vector<int64_t> & inEntryPks);

  /** What it shows (tests): the status ("" when it shows an entry), the
      package view, whether it is reading. */
  std::string status() const;
  PackageView * packageView() { return mPackage; }
  RichView * richView() { return mPreview; }
  bool loading() const { return mLoading; }

private:
  void showStatus(const std::string & inText);

  Fl_Box * mStatus = nullptr;
  PackageView * mPackage = nullptr;
  RichView * mPreview = nullptr;
  bool mLoading = false;
  unsigned mGeneration = 0;
  std::shared_ptr<bool> mAlive;
  std::filesystem::path mFile;
  std::vector<int64_t> mEntries;
};

} // namespace ncx

#endif // NCX_UI_INSPECTOR_H
