/*
	File:		NCPrefsViewController.h

	Contains:	Preferences controller for the NCX app.

	Written by:	Newton Research Group, 2015.
*/

#import <Cocoa/Cocoa.h>
#import "PreferenceKeys.h"

@interface NCPrefsViewController : NSViewController
@property(readonly) NSUserDefaults * sharedUserDefaults;
@end
