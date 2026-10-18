/*
 File: RestoreDialog.cc

 The restore dialog (RestoreDialog.h).
 */

#include "ui/RestoreDialog.h"

#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Browser.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Return_Button.H>

namespace ncx {

namespace {

std::string MenuLabel(const std::string & inText)
{
  std::string label;
  for (char c : inText) {
    if (c == '/' || c == '\\' || c == '&' || c == '_')
      label += '\\';
    label += c;
  }
  return label;
}

} // namespace


RestoreDialog::RestoreDialog(const std::vector<RestoreChoice> & inChoices)
  : RestoreDialogUI(0, 0, 460, 470, "Restore"), mChoices(inChoices)
{
  for (const RestoreChoice & choice : mChoices)
    mTarget->add(MenuLabel(choice.store.name).c_str());
  mTarget->value(0);
  mTarget->callback(TargetCB, this);
  mSource->callback(SourceCB, this);
  mAllApps->callback(AllCB, mApps);
  mAllPackages->callback(AllCB, mPackages);
  mCancel->callback(CloseCB, this);
  mCancel->argument(0);
  mCancel->shortcut(FL_Escape);
  mRestore->callback(CloseCB, this);
  mRestore->argument(1);
  callback(CloseCB, this);
  argument(0);
  set_modal();
  showSources();
}


void RestoreDialog::showSources()
{
  mSource->clear();
  int target = mTarget->value();
  if (target >= 0 && target < (int)mChoices.size())
    for (const RestoreFrom & from : mChoices[target].sources)
      mSource->add(MenuLabel(from.store.name).c_str());
  mSource->value(0);
  showSource();
}


void RestoreDialog::showSource()
{
  mApps->clear();
  mPackages->clear();
  int target = mTarget->value(), source = mSource->value();
  if (target < 0 || target >= (int)mChoices.size() || source < 0 || source >= (int)mChoices[target].sources.size()) {
    mRestore->deactivate();
    return;
  }
  const RestoreFrom & from = mChoices[target].sources[source];
  for (const AppRecord & app : from.apps)
    mApps->add(app.name.c_str(), 1);
  for (const EntryRecord & package : from.packages)
    mPackages->add(package.title ? package.title->c_str() : "(a package)", 0);
  mRestore->activate();
  redraw();
}


RestoreSelection RestoreDialog::selection() const
{
  RestoreSelection selection;
  int target = mTarget->value(), source = mSource->value();
  if (target < 0 || target >= (int)mChoices.size() || source < 0 || source >= (int)mChoices[target].sources.size())
    return selection;
  const RestoreFrom & from = mChoices[target].sources[source];
  selection.target = mChoices[target].store;
  selection.source = from.store;
  for (size_t i = 0; i < from.apps.size(); ++i)
    if (mApps->checked((int)i + 1))
      selection.apps.push_back(from.apps[i]);
  for (size_t i = 0; i < from.packages.size(); ++i)
    if (mPackages->checked((int)i + 1))
      selection.packages.push_back(from.packages[i]);
  return selection;
}


std::optional<RestoreSelection> RestoreDialog::run()
{
  mAnswer = 0;
  show();
  while (shown())
    Fl::wait();
  if (mAnswer != 1)
    return std::nullopt;
  RestoreSelection chosen = selection();
  if (chosen.apps.empty() && chosen.packages.empty())
    return std::nullopt;
  return chosen;
}


void RestoreDialog::TargetCB(Fl_Widget *, void * inDialog)
{
  static_cast<RestoreDialog *>(inDialog)->showSources();
}


void RestoreDialog::SourceCB(Fl_Widget *, void * inDialog)
{
  static_cast<RestoreDialog *>(inDialog)->showSource();
}


void RestoreDialog::AllCB(Fl_Widget *, void * inBrowser)
{
  static_cast<Fl_Check_Browser *>(inBrowser)->check_all();
}


void RestoreDialog::CloseCB(Fl_Widget * inWidget, void * inDialog)
{
  RestoreDialog * dialog = static_cast<RestoreDialog *>(inDialog);
  dialog->mAnswer = (int)inWidget->argument();
  dialog->hide();
}

} // namespace ncx
