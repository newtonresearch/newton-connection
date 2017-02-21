/*
	File:		AppDelegate.h

	Contains:	Cocoa controller delegate declarations for the NCX app.

	Written by:	Newton Research Group, 2005.
*/

#import <Cocoa/Cocoa.h>


/*------------------------------------------------------------------------------
	N C X C o n t r o l l e r
------------------------------------------------------------------------------*/

@protocol NCAppSleepProtocol
- (BOOL)applicationCanSleep;
- (void)applicationWillSleep;
@end


@interface NCXController : NSObject<NCAppSleepProtocol, NSOpenSavePanelDelegate>
{
	NSMenuItem * currentNOS1MenuItem;
}

// NSApplication delegate methods
- (void)applicationDidFinishLaunching:(NSNotification *)notification;

// Sleep/Wake
- (BOOL)applicationCanSleep;
- (void)applicationWillSleep;
- (void)applicationWillWake;

// menu items
- (IBAction)installPackage:(id)sender;
- (IBAction)dumpROM:(id)sender;
- (IBAction)selectNewton1Session:(id)sender;

@end


