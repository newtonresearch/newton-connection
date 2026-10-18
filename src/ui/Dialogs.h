/*
 File: Dialogs.h

 The app's small dialogs (PORTING.md 7.7): a warning the user may silence
 ("Don't show this again", the Cocoa app's NSAlert suppression button).
 The UI thread only.
 */

#ifndef NCX_UI_DIALOGS_H
#define NCX_UI_DIALOGS_H

#include <string>

namespace ncx {

/** Ask before going on, unless silenced: inMessage (bold), inDetail, a
    "Don't show this again" check box (it sets inPrefKey), Cancel and
    inGoLabel. True: go on (also when silenced before). */
bool ConfirmUnlessSilenced(const char * inPrefKey, const std::string & inTitle, const std::string & inMessage,
                           const std::string & inDetail, const std::string & inGoLabel);

} // namespace ncx

#endif // NCX_UI_DIALOGS_H
