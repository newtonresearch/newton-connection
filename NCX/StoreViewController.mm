/*
	File:		StoreViewController.mm

	Abstract:	Implementation of NCStoreViewController.

	Written by:		Newton Research, 2011.
*/

#import "StoreViewController.h"

extern NSDateFormatter * gDateFormatter;


/* -----------------------------------------------------------------------------
	N C S t o r e V i e w C o n t r o l l e r
	The Store Info view is essentially static.
	But we do accept drops of .pkg files for installation.
	So we need to:
	[self.view registerForDraggedTypes:@[NSFilenamesPboardType]];
	and respond to:
- (NSDragOperation) draggingEntered: (id <NSDraggingInfo>) sender
- (BOOL) performDragOperation: (id <NSDraggingInfo>) sender
	Create NCDragBox : NSBox
		which sends those messages on to its delegate -- this controller.
	and do this for device and keyboard too.
----------------------------------------------------------------------------- */

@implementation NCStoreViewController

- (void)viewWillAppear {
	[super viewWillAppear];
	// we only accept dropped packages (to install) so we MUST be tethered
	if (gNCNub.isTethered) {
		[self.view registerForDraggedTypes:@[(NSString *)kUTTypeFileURL]];
		isRegisteredForDraggedTypes = YES;
	}
}

- (void)viewWillDisappear {
	if (isRegisteredForDraggedTypes)
		[self.view unregisterDraggedTypes];
	[super viewWillDisappear];
}


/*------------------------------------------------------------------------------
	Indicate that our view accepts dragged packages (to be installed).
	We must be the view’s delegate to receive this.
	Args:		sender
	Return:	our willingness to accept the drag
------------------------------------------------------------------------------*/

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
	NSArray * classes = @[NSURL.class];
	NSDictionary * options = @{ NSPasteboardURLReadingFileURLsOnlyKey:[NSNumber numberWithBool:YES],
										 NSPasteboardURLReadingContentsConformToTypesKey:@[@"com.newton.package", @"com.apple.installer-package-archive"] };

	if ([sender.draggingPasteboard canReadObjectForClasses:classes options:options]) {
		return NSDragOperationCopy;
	}
	return NSDragOperationNone;
}


/*------------------------------------------------------------------------------
	If package files were dropped, install them.
	We must be the view’s delegate to receive this.
	Args:		sender
	Return:	YES always
------------------------------------------------------------------------------*/

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
	NSArray * classes = @[NSURL.class];
	NSDictionary * options = @{NSPasteboardURLReadingFileURLsOnlyKey: [NSNumber numberWithBool:YES],
										NSPasteboardURLReadingContentsConformToTypesKey: [NSArray arrayWithObjects:@"com.newton.package", @"com.apple.installer-package-archive", nil]};

	NSArray * urls = [sender.draggingPasteboard readObjectsForClasses:classes options:options];
	if (urls && gNCNub.isTethered) {
		[gNCNub installPackages:urls];
	}

	return YES;
}

@end
