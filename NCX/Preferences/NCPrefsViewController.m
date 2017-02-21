/*
	File:		NCPrefsViewController.m

	Contains:	Preference controller for the NCX app.

	Written by:	Newton Research Group, 2008.
*/

#import "NCPrefsViewController.h"

@implementation NCPrefsViewController
- (NSUserDefaults *) sharedUserDefaults {
	return NSUserDefaults.standardUserDefaults;
}
@end
