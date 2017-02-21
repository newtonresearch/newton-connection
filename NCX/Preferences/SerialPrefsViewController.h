/*
	File:		SerialPrefsViewController.h

	Contains:	Serial preferences view controller for the NCX app.

	Written by:	Newton Research Group, 2015.
*/

#import "NCPrefsViewController.h"

@interface SerialPrefsViewController : NCPrefsViewController
{
	IBOutlet NSPopUpButton * serialPortPopup;
}

+ (int)preferredSerialPort:(NSString *__strong *)outPort bitRate:(NSUInteger *)outRate;

//	serial prefs
- (IBAction)updateSerialPort:(id)sender;
@end
