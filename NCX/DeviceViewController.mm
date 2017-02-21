/*
	File:		DeviceViewController.mm

	Abstract:	Implementation of NCDeviceViewController.

	Written by:		Newton Research, 2011.
*/

#import "BackupDocument.h"
#import "DeviceViewController.h"
#import "NCWindowController.h"

/* -----------------------------------------------------------------------------
	N C D e v i c e V i e w C o n t r o l l e r
	The Device Info view offers Sync and Restore services.
----------------------------------------------------------------------------- */

@implementation NCDeviceViewController

@synthesize canSync;
@synthesize canRestore;

- (void)viewDidLoad {
	[super viewDidLoad];

	manufactureInfoIndex = 0;
	serialNumberInfoIndex = [[self.representedObject serialNumber] isEqualToString:@"0000-0000-0000-0000"] ? 0 : 1;
	versionInfoIndex = 1;

	[self willChangeValueForKey:@"serialNumberInfo"];
	[self didChangeValueForKey:@"serialNumberInfo"];
	[self willChangeValueForKey:@"serialNumberLabel"];
	[self didChangeValueForKey:@"serialNumberLabel"];

	[self willChangeValueForKey:@"versionInfo"];
	[self didChangeValueForKey:@"versionInfo"];
	[self willChangeValueForKey:@"versionLabel"];
	[self didChangeValueForKey:@"versionLabel"];
}


- (void)viewWillAppear {
	[super viewWillAppear];

	if (self.document.isReadOnly || self.document.isNewton1) {
		self.canSync = NO;
		self.canRestore = NO;
	} else {
		[self enableSync];
		[self enableRestore];
		// we only accept dropped packages (to install) so we MUST be tethered
		if (gNCNub.isTethered) {
			[self.view registerForDraggedTypes:@[(NSString *)kUTTypeFileURL]];
			isRegisteredForDraggedTypes = YES;
			[NSNotificationCenter.defaultCenter addObserver:self
																  selector:@selector(operationDone:)
																		name:kDockDidOperationNotification
																	 object:self.document];
		}
	}
}

- (void)viewWillDisappear {

	if (gNCNub.operationInProgress == kNoActivity) {
		[self.view.window.windowController stopProgress];
	}
	if (isRegisteredForDraggedTypes) {
		[self.view unregisterDraggedTypes];
		[NSNotificationCenter.defaultCenter removeObserver:self];
	}
	[super viewWillDisappear];
}


/* -----------------------------------------------------------------------------
	Change the manufacture information.
	Args:		sender
	Return:	--
----------------------------------------------------------------------------- */

- (IBAction)nextManufactureInfo:(id)sender {
	if ([self.representedObject manufactureDate] != nil) {
		[self willChangeValueForKey:@"manufactureInfo"];
		[self willChangeValueForKey:@"manufactureLabel"];

		if (++manufactureInfoIndex > 1)
			manufactureInfoIndex = 0;

		[self didChangeValueForKey:@"manufactureLabel"];
		[self didChangeValueForKey:@"manufactureInfo"];
	}
}

- (NSString *)manufactureLabel {
	return manufactureInfoIndex == 0 ? @"Manufacturer:" : @"Manufactured:";
}

extern NSDateFormatter * gDateFormatter;
- (NSString *)manufactureInfo; {
	return manufactureInfoIndex == 0 ? [self.representedObject manufacturer]
												: [gDateFormatter stringFromDate: [self.representedObject manufactureDate]];
}


/* -----------------------------------------------------------------------------
	Change the serial number information.
	Args:		sender
	Return:	--
----------------------------------------------------------------------------- */

- (IBAction)nextSerialNumberInfo:(id)sender {
	if (![[self.representedObject serialNumber] isEqualToString:@"0000-0000-0000-0000"]) {
		[self willChangeValueForKey:@"serialNumberInfo"];
		[self willChangeValueForKey:@"serialNumberLabel"];

		if (++serialNumberInfoIndex > 1)
			serialNumberInfoIndex = 0;

		[self didChangeValueForKey:@"serialNumberLabel"];
		[self didChangeValueForKey:@"serialNumberInfo"];
	}
}

- (NSString *)serialNumberLabel {
	return serialNumberInfoIndex == 0 ? @"ID:" : @"Serial Number:";
}

- (NSString *)serialNumberInfo {
	return serialNumberInfoIndex == 0 ? [self.representedObject newtonId]
												 : [self.representedObject serialNumber];
}


/* -----------------------------------------------------------------------------
	Change the version number information.
	Args:		sender
	Return:	--
----------------------------------------------------------------------------- */

- (IBAction)nextVersionInfo:(id)sender {
	if (![[self.representedObject is1xData] boolValue]) {
		[self willChangeValueForKey:@"versionInfo"];
		[self willChangeValueForKey:@"versionLabel"];

		if (++versionInfoIndex > 1)
			versionInfoIndex = 0;

		[self didChangeValueForKey:@"versionLabel"];
		[self didChangeValueForKey:@"versionInfo"];
	}
}

- (NSString *)versionLabel {
	return versionInfoIndex == 0 ? @"ROM Version:" : @"OS Version:";
}

- (NSString *)versionInfo {
	return versionInfoIndex == 0 ? [self.representedObject ROMversion]
										  : [self.representedObject OSversion];
}

+ (NSSet<NSString *> *)keyPathsForValuesAffectingVersionInfo {
	return [NSSet setWithObject:@"self.representedObject.OSversion"];
}


/*------------------------------------------------------------------------------
	Indicate that our view accepts dragged packages (to be installed).
	We must be the view’s delegate to receive this.
	Args:		sender
	Return:	our willingness to accept the drag
------------------------------------------------------------------------------*/

- (NSDragOperation)draggingEntered:(id <NSDraggingInfo>)sender {
	NSArray * classes = @[NSURL.class];
	NSDictionary * options = @{ NSPasteboardURLReadingFileURLsOnlyKey: [NSNumber numberWithBool:YES],
										 NSPasteboardURLReadingContentsConformToTypesKey: @[@"com.newton.package", @"com.apple.installer-package-archive"] };

	if ([sender.draggingPasteboard canReadObjectForClasses:classes options:options])
		return NSDragOperationCopy;
	return NSDragOperationNone;
}


/* -----------------------------------------------------------------------------
	If package files were dropped, install them.
	We must be the view’s delegate to receive this.
	Args:		sender
	Return:	YES always
----------------------------------------------------------------------------- */

- (BOOL)performDragOperation:(id <NSDraggingInfo>)sender {
	NSArray * classes = @[NSURL.class];
	NSDictionary * options = @{ NSPasteboardURLReadingFileURLsOnlyKey: [NSNumber numberWithBool:YES],
										 NSPasteboardURLReadingContentsConformToTypesKey: @[@"com.newton.package", @"com.apple.installer-package-archive"] };

	NSArray * urls = [sender.draggingPasteboard readObjectsForClasses:classes options:options];
	if (urls && gNCNub.isTethered) {
		[gNCNub installPackages:urls];
	}

	return YES;
}


#pragma mark Sync
/* -----------------------------------------------------------------------------
	If we’re tethered and not doing anything else, enable the sync(backup) button.
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (void)enableSync {
	self.canSync = (gNCNub.isTethered && gNCNub.operationInProgress == kNoActivity);
}


/* -----------------------------------------------------------------------------
	The Sync button is pressed -- start the sync(backup) operation protocol.
	Args:		sender
	Return:	--
----------------------------------------------------------------------------- */

- (IBAction)startSync:(id)sender {
	self.canSync = NO;
	self.canRestore = NO;
	// disable sourceListView navigation

	//	pass request on to dock controller
	[gNCNub requestSync];
}


/* -----------------------------------------------------------------------------
	The backup/restore operation is done.
----------------------------------------------------------------------------- */

- (void)operationDone:(NSNotification *)inNotification {

	NSNumber * err = inNotification.userInfo[@"error"];
	self.document.errorStatus = err.intValue;

	[self.view.window.windowController stopProgress];

	[self enableSync];
	[self enableRestore];
}


#pragma mark Restore
/* -----------------------------------------------------------------------------
	If we’re tethered and have a backup, enable the restore button.
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (void)enableRestore {
	self.canRestore = (gNCNub.isTethered && gNCNub.operationInProgress == kNoActivity);
}


/* -----------------------------------------------------------------------------
	The Restore button is pressed -- start the restore protocol.
	First we need to choose WHAT to restore.
	Args:		sender
	Return:	--
----------------------------------------------------------------------------- */

- (void)prepareForSegue:(NSStoryboardSegue *)segue sender:(id)sender {
	NCRestoreViewController * toViewController = (NCRestoreViewController *)segue.destinationController;
	toViewController.document = self.document;

	self.canSync = NO;
	self.canRestore = NO;
	[self.document buildRestoreInfo];
}

@end


#pragma mark -
/* -----------------------------------------------------------------------------
	N C I n f o T e x t
	A NSTextField that accepts clicks.
----------------------------------------------------------------------------- */

@implementation NCInfoTextField
 
/* -----------------------------------------------------------------------------
	Cycle through the various info available on the device.
----------------------------------------------------------------------------- */

- (void)mouseUp:(NSEvent *)inEvent {
	[self sendAction:self.action to:self.target];
}

@end


/* -----------------------------------------------------------------------------
	N C R e s t o r e V i e w C o n t r o l l e r
	Controller for the restore info sheet.
----------------------------------------------------------------------------- */

@implementation NCRestoreViewController

- (IBAction)selectAllApps:(id)sender {
	NSMutableArray * apps = self.document.restoreInfo.restore[@"apps"];
	NSArray * selectedApps = [apps filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"isSelected = YES"]];
	if (selectedApps.count >= apps.count) {
		// they’re mostly selected, so deselect all
		for (NCApp * app in apps)
			app.isSelected = NO;
	} else {
		// they’re mostly deselected, so select all
		for (NCApp * app in apps)
			app.isSelected = YES;
	}
}


- (IBAction)selectAllPkgs:(id)sender {
	NSMutableArray * pkgs = self.document.restoreInfo.restore[@"pkgs"];
	NSArray * selectedPkgs = [pkgs filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"isSelected = YES"]];
	if (selectedPkgs.count >= pkgs.count) {
		// they’re mostly selected, so deselect all
		for (NCEntry * pkg in pkgs)
			pkg.isSelected = NO;
	} else {
		// they’re mostly deselected, so select all
		for (NCEntry * pkg in pkgs)
			pkg.isSelected = YES;
	}
}


/*------------------------------------------------------------------------------
	The restore chooser was dismissed. End the sheet.
	Args:		sender
	Return:	--
------------------------------------------------------------------------------*/

- (IBAction)restoreSelection:(id)sender {
	NSModalResponse response = ((NSControl *)sender).tag;

	if (response == NSModalResponseOK) {
		//	pass request on to dock controller
		[gNCNub requestRestore];
	} else {
		[NSNotificationCenter.defaultCenter postNotificationName:kDockDidOperationNotification object:self.document userInfo:@{@"operation":[NSNumber numberWithInt:kRestoreActivity],@"error":[NSNumber numberWithInt:noErr]}];
	}

	[self.presentingViewController dismissViewController:self];
}

@end
