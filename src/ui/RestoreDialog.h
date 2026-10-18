/*
 File: RestoreDialog.h

 The restore dialog (PORTING.md 7.7; the Cocoa app's restore sheet,
 NCRestoreViewController): which of the Newton's stores to restore, from
 which backed-up store of its kind, which apps and which packages (all
 apps chosen at first, Select All for each). Restore gives the selection.
 Its layout: RestoreDialogUI.fl.
 */

#ifndef NCX_UI_RESTOREDIALOG_H
#define NCX_UI_RESTOREDIALOG_H

#include "RestoreDialogUI.h"

#include "dock/Restore.h"
#include "model/Restore.h"

#include <optional>
#include <vector>

namespace ncx {

class RestoreDialog : public RestoreDialogUI
{
public:
  explicit RestoreDialog(const std::vector<RestoreChoice> & inChoices);

  /** Shown until Restore (the selection) or Cancel (none). */
  std::optional<RestoreSelection> run();
  /** The selection as the dialog has it now (tests). */
  RestoreSelection selection() const;

private:
  void showSources();
  void showSource();
  static void TargetCB(Fl_Widget *, void *);
  static void SourceCB(Fl_Widget *, void *);
  static void AllCB(Fl_Widget *, void *);
  static void CloseCB(Fl_Widget *, void *);

  std::vector<RestoreChoice> mChoices;
  int mAnswer = 0;
};

} // namespace ncx

#endif // NCX_UI_RESTOREDIALOG_H
