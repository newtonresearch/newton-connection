/*
	File:		AppDelegate.mm

	Contains:	Cocoa controller delegate for the NCX app.
					The app delegate needs to:
					o  set up formatters for the UI
					o  set up value transformers for the UI
					o  register user defaults
					o  handle wake/sleep

	Written by:	Newton Research Group, 2005.
*/

#import "AppDelegate.h"
#import "PreferenceKeys.h"
#import "NCDocument.h"
#import "NCDockProtocolController.h"
#import "PlugInUtilities.h"
#import "Utilities.h"

extern void	CaptureStdioOutTranslator(const char * inFilename);
extern void	EndCaptureStdioOutTranslator(void);


/*------------------------------------------------------------------------------
	D a t a
------------------------------------------------------------------------------*/

int gLogLevel;
NSNumberFormatter * gNumberFormatter;
NSDateFormatter * gDateFormatter;


/*------------------------------------------------------------------------------
	V a l u e T r a n s f o r m e r
------------------------------------------------------------------------------*/

@interface NCWarningValueTransformer : NSValueTransformer
@end

@implementation NCWarningValueTransformer
+ (Class)transformedValueClass {
	return [NSNumber class];
}
+ (BOOL)allowsReverseTransformation {
	return NO;
}
- (id)transformedValue:(id)inValue {
	return [NSNumber numberWithDouble: [inValue doubleValue] * 0.8];
}
@end


@interface NCCriticalValueTransformer : NSValueTransformer
@end

@implementation NCCriticalValueTransformer
+ (Class)transformedValueClass {
	return [NSNumber class];
}
+ (BOOL)allowsReverseTransformation {
	return NO;
}
- (id)transformedValue:(id)inValue {
	return [NSNumber numberWithDouble: [inValue doubleValue] * 0.9];
}
@end


@interface NCDateValueTransformer : NSValueTransformer
@end

@implementation NCDateValueTransformer
+ (Class)transformedValueClass {
	return [NSString class];
}
+ (BOOL)allowsReverseTransformation {
	return NO;
}
- (id)transformedValue:(id)inValue {
	// inValue is a (numerical) Newton date
	NSDate * dateValue = MakeNSDate(MAKEINT([inValue longValue]));
	if (dateValue == nil)
		return @"--";
	return [gDateFormatter stringFromDate:dateValue];
}
@end


@interface NCArrayIsEmpty : NSValueTransformer
@end

@implementation NCArrayIsEmpty
+ (Class)transformedValueClass {
	return [NSNumber class];
}
+ (BOOL)allowsReverseTransformation {
	return NO;
}
- (id)transformedValue:(id)inValue {
	return [NSNumber numberWithBool:[(NSArray *)inValue count] < 2];
}
@end


/*------------------------------------------------------------------------------
	N C X C o n t r o l l e r
------------------------------------------------------------------------------*/

@implementation NCXController

/*------------------------------------------------------------------------------
	Application is up; register our user defaults.
	Args:		inNotification
	Return:	--
------------------------------------------------------------------------------*/

- (void) applicationDidFinishLaunching: (NSNotification *) inNotification
{
	NSUserDefaults * userDefaults = NSUserDefaults.standardUserDefaults;
	[userDefaults registerDefaults:@{
	//	General
		kLogLevelPref: @"1",
		kROMAddrPref: @"8388608",	// 8*MByte start address (1*MByte = 1048576)
		kROMSizePref: @"8388608",	// 8*MByte in size

		kUserFontName: @"Casual",
		kUserFontSize: @"18.0",

		kAutoBackupPref: @"NO",
		kSelectiveBackupPref: @"NO",
		kAutoSyncPref: @"NO",
		kNoSyncWarningPref: @"NO",
	//	kSerialPortPref: @"",
		kSerialBaudPref: @"38400",
	//	Security
		kPasswordPref: @"",
	//	Software Update
	// Sparkle keys are private wef v1.5
	}];

	if ([userDefaults boolForKey:kLogToFilePref]) {
		NSURL * url = ApplicationLogFile();
		CaptureStdioOutTranslator(url.fileSystemRepresentation);
	}
	gLogLevel = (int)[userDefaults integerForKey:kLogLevelPref];

//	[userDefaults setInteger:kSynchronizeSession forKey:kNewton1SessionType];
	int nOS1MenuItemTag = (int)[userDefaults integerForKey:kNewton1SessionType];
	if (nOS1MenuItemTag < 2 || nOS1MenuItemTag > 4)
		nOS1MenuItemTag = 2;
	NSMenu * nOS1Menu = [[NSApp.mainMenu itemWithTag:111] submenu];
	currentNOS1MenuItem = [nOS1Menu itemWithTag:nOS1MenuItemTag];
	[currentNOS1MenuItem setState:NSOnState];

	// initialize the number formatter used throughout the UI
	gNumberFormatter = [[NSNumberFormatter alloc] init];
	[gNumberFormatter setNumberStyle:NSNumberFormatterDecimalStyle];

	// initialize the date formatter used throughout the UI
	gDateFormatter = [[NSDateFormatter alloc] init];
	[gDateFormatter setDateStyle:NSDateFormatterMediumStyle];
	[gDateFormatter setTimeStyle:NSDateFormatterShortStyle];

	// initialize the value transformers used throughout the application bindings
	[NSValueTransformer setValueTransformer:[[NCWarningValueTransformer alloc] init] forName:@"NCWarningValueTransformer"];
	[NSValueTransformer setValueTransformer:[[NCCriticalValueTransformer alloc] init] forName:@"NCCriticalValueTransformer"];
	[NSValueTransformer setValueTransformer:[[NCDateValueTransformer alloc] init] forName:@"NCDateValueTransformer"];
	[NSValueTransformer setValueTransformer:[[NCArrayIsEmpty alloc] init] forName:@"NCArrayIsEmpty"];
}


/* -----------------------------------------------------------------------------
	Open a connection window when we launch.
	Args:		sender
	Return:	always YES
----------------------------------------------------------------------------- */

- (BOOL)applicationShouldOpenUntitledFile:(NSApplication *)sender {
	return YES;
}


/*------------------------------------------------------------------------------
	If there’s a transaction in progress we shouldn’t go to sleep.
	If there’s an open session it should be closed and reopened when system wakes.
	Args:		--
	Return:	YES => no operation in progress
	TO DO:	this, properly
------------------------------------------------------------------------------*/

- (BOOL)applicationCanSleep {
	return gNCNub == nil || gNCNub.operationInProgress == kNoActivity;
}

- (void)applicationWillSleep {
	if (gNCNub)
		[gNCNub disconnect];
}

- (void)applicationWillWake {
/* not sure we really want to do this
	if (gNCNub)
		[gNCNub listen];
*/
}

/*------------------------------------------------------------------------------
	Install packages (which is equivalent to “open files”) when package files
	are dropped on our icon.
	Args:		sender
				filenames
	Return:	always YES
------------------------------------------------------------------------------*/
#if 0
// Don’t do this.
// If you do, you’ll have to open recent documents too otherwise Open Recent
// will not work. Probably not what we want to do anyway.
- (void)application:(NSApplication *)sender openFiles:(NSArray *)filenames {
	NCDocument * document = [NCDocument activeDocument];
	if (document && document.dock.isTethered) {
		NSMutableArray * urls = [NSMutableArray arrayWithCapacity:filenames.count];
		// convert filenames to URLs and verify they are packages
		for (NSString * filename in filenames) {
			NSURL * anURL = [NSURL fileURLWithPath:filename];
			if (anURL) {
				NSString * itemUTI = nil;
				NSError *__autoreleasing error = nil;
				if ([anURL getResourceValue:&itemUTI forKey:NSURLTypeIdentifierKey error:&error])
					if (UTTypeConformsTo((CFStringRef)itemUTI, (CFStringRef)@"com.newton.package"))
						[urls addObject:anURL];
			}
		}

		if (urls.count > 0) {
			[document.dock installPackages:urls];
			[NSApp replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
			return;
		}
	}
	[NSApp replyToOpenOrPrint:NSApplicationDelegateReplyFailure];
}
#endif

/*------------------------------------------------------------------------------
	Defer termination until we’re properly disconnected.
	We want to tell newton we’re disconnecting, and disconnect cleanly when the
	application terminates.
	Args:		sender
	Return:	NSTerminateNow
------------------------------------------------------------------------------*/

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {

	[self applicationWillSleep];

	// need to wait for comms termination?
	//		reply w/ NSTerminateLater then [NSApp replyToApplicationShouldTerminate:YES]
	[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];

	EndCaptureStdioOutTranslator();

	return NSTerminateNow;
}


/* -----------------------------------------------------------------------------
	The menu items we are responsible for are:
	File
		New					only allow new document if none already open
		Install Package	choose .pkg files and install them
		Dump Newton ROM	choose file to contain ROM dump, dump it

	Enable main menu items as per logic above.
	Args:		inItem
	Return:	YES => enable
----------------------------------------------------------------------------- */

- (BOOL)validateUserInterfaceItem:(id<NSValidatedUserInterfaceItem>)inItem {
// File menu
	// we can install a package if we’re not doing anything else
	if ([inItem action] == @selector(installPackage:))
		return gNCNub && gNCNub.isTethered && gNCNub.operationInProgress == kNoActivity;
	// we can dump the ROM if we’re not doing anything else
	if ([inItem action] == @selector(dumpROM:))
		return gNCNub && gNCNub.isTethered && gNCNub.operationInProgress == kNoActivity;
	return YES;
}


/* -----------------------------------------------------------------------------
	Let user choose package files for installation.
	Args:		sender
	Return:	--
----------------------------------------------------------------------------- */

- (IBAction)installPackage:(id)sender {
	NSOpenPanel * chooser = [NSOpenPanel openPanel];
	chooser.delegate = self;
	chooser.title = NSLocalizedString(@"select package", @"package chooser dialog title");

	chooser.allowsMultipleSelection = YES;
	chooser.allowedFileTypes = [NSArray arrayWithObjects: @"com.newton.package", @"com.apple.installer-package-archive", nil];

	if ([chooser runModal] == NSModalResponseOK)
		[gNCNub installPackages:chooser.URLs];
}

//@protocol NSOpenSavePanelDelegate <NSObject>
/* Optional - enabled URLs.
    NSOpenPanel: Return YES to allow the 'url' to be enabled in the panel. Delegate implementations should be fast to avoid stalling the UI. Applications linked on Mac OS 10.7 and later should be prepared to handle non-file URL schemes.
    NSSavePanel: This method is not called; all urls are always disabled.

- (BOOL)panel:(id)sender shouldEnableURL:(NSURL *)url
{
	;
}
*/
/* Optional - URL validation for saving and opening files. 
    NSSavePanel: The method is called once by the save panel when the user chooses the Save button. The user is intending to save a file at 'url'. Return YES if the 'url' is a valid location to save to. Note that an item at 'url' may not physically exist yet, unless the user decided to overwrite an existing item. Return NO and fill in the 'outError' with a user displayable error message for why the 'url' is not valid. If a recovery option is provided by the error, and recovery succeeded, the panel will attempt to close again.
    NSOpenPanel: The method is called once for each selected filename (or directory) when the user chooses the Open button. Return YES if the 'url' is acceptable to open. Return NO and fill in the 'outError' with a user displayable message for why the 'url' is not valid for opening. You would use this method over panel:shouldEnableURL: if the processing of the selected item takes a long time. If a recovery option is provided by the error, and recovery succeeded, the panel will attempt to close again.
*/
- (BOOL)panel:(id)sender validateURL:(NSURL *)inURL error:(NSError **)outError {
	if (IsNewtonPkg(inURL)) {
		return YES;
	}
	if (outError) {
		*outError = [NSError errorWithDomain:NSCocoaErrorDomain code:NSFileReadCorruptFileError userInfo:nil];
	}
	return NO;
}


/* -----------------------------------------------------------------------------
	Extract ROM and dump to file.
	Args:		sender
	Return:	--
----------------------------------------------------------------------------- */

- (IBAction)dumpROM:(id)sender {
	NSSavePanel * chooser = [NSSavePanel savePanel];
	chooser.title = @"Dump Newton ROM";

	// preset ~/Downloads/NewtonROM
	chooser.directoryURL = [[NSFileManager defaultManager] URLForDirectory:NSDownloadsDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:NO error:nil];;
	chooser.nameFieldStringValue = @"NewtonROM";
	chooser.canCreateDirectories = YES;

	if ([chooser runModal] == NSModalResponseOK)
		[gNCNub dumpROM:chooser.URL];
}


/* -----------------------------------------------------------------------------
	Select the type of session to initiate when we connect to a Newton 1 device.
	Args:		sender
	Return:	--
----------------------------------------------------------------------------- */

- (IBAction)selectNewton1Session:(id)sender {
	// maintain the check mark
	[currentNOS1MenuItem setState:NSOffState];
	currentNOS1MenuItem = sender;
	[sender setState:NSOnState];

	// remember choice as preference in user defaults
	[NSUserDefaults.standardUserDefaults setInteger:[(NSControl *)sender tag] forKey:kNewton1SessionType];

	// in addition, if choosing to install a package, choose that package now
	if ([(NSControl *)sender tag] == kLoadPackageSession) {
		NSOpenPanel * chooser = [NSOpenPanel openPanel];
		chooser.title = NSLocalizedString(@"select package", @"package chooser dialog title");
		chooser.allowsMultipleSelection = NO;
		chooser.allowedFileTypes = [NSArray arrayWithObjects:@"com.newton.package",@"pkg",@"PKG",nil];
		if ([chooser runModal] == NSModalResponseOK
		&&  chooser.URLs.count > 0)
			// remember the chosen file so we can install it when a connection is established
			gNCNub.pkgURL = chooser.URLs[0];
		else
			gNCNub.pkgURL = nil;
	}
}


/*------------------------------------------------------------------------------
	Handle Report Bugs application menu item.
	Args:		sender
	Return:	--
------------------------------------------------------------------------------*/

- (IBAction)reportBugs:(id)sender {
	NSURL * url = [NSURL URLWithString:@"mailto:simon@newtonresearch.org?subject=Newton%20Connection%20Bug%20Report" /*"&body=Share%20and%20Enjoy"*/ ];
	[NSWorkspace.sharedWorkspace openURL:url];
}

@end
