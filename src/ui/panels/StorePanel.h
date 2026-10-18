/*
 File: StorePanel.h

 The store panel (PORTING.md 7.4; the Cocoa app's NCStoreViewController):
 a store's picture (internal or card), kind, status (default, password
 protected, read only), signature, capacity, used and free, and how full
 it is (green, then yellow from 80 %, red from 90 %, as the Cocoa app's
 level indicator). Its layout: StorePanelUI.fl.
 */

#ifndef NCX_UI_PANELS_STOREPANEL_H
#define NCX_UI_PANELS_STOREPANEL_H

#include "StorePanelUI.h"

#include "model/Records.h"

#include <string>

namespace ncx {

class StorePanel : public StorePanelUI
{
public:
  StorePanel(int inX, int inY, int inW, int inH);

  /** Show the store. */
  void update(const StoreRecord & inStore);
};

/** "Default store", "Default store, password protected.", "Password
    protected.", "--" (the Cocoa app's), and ", read only". */
std::string StoreStatusText(const StoreRecord & inStore);

} // namespace ncx

#endif // NCX_UI_PANELS_STOREPANEL_H
