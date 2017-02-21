/*
	File:		KeyboardViewController.m

	Abstract:	Implementation of NCKeyboardInfo and NCKeyboardViewController.

	Written by:		Newton Research, 2011.
*/

#import "KeyboardViewController.h"
#import "NCDocument.h"
#import "NCWindowController.h"


/* -----------------------------------------------------------------------------
	N C K e y b o a r d I n f o
	We just need the class.
----------------------------------------------------------------------------- */

@implementation NCKeyboardInfo
- (NSString *)name {
	return @"Keyboard";
}

- (NSImage *)image {
	return [NSImage imageNamed:@"source-keyboard.png"];
}

- (NSString *)identifier {
	return @"Keyboard";
}

@end


#pragma mark -
/* -----------------------------------------------------------------------------
	N C K e y b o a r d V i e w C o n t r o l l e r
	Controller for the keyboard passthrough info pane.
----------------------------------------------------------------------------- */
@interface NCKeyboardViewController ()
{
	NSResponder * lastResponder;
}
@end

@implementation NCKeyboardViewController

/* -----------------------------------------------------------------------------
	Show the keyboard info panel and start the keyboard passthrough protocol.
----------------------------------------------------------------------------- */

- (void)viewWillAppear {
	[super viewWillAppear];
	lastResponder = nil;

	if (gNCNub.isTethered) {
		// for now we accept drops of text, but should we also accept text files?
		[self.view registerForDraggedTypes:@[NSPasteboardTypeString]];
		isRegisteredForDraggedTypes = YES;

		// establish keyboard passthrough w/ session
		self.instructions = @"Please wait…";
		self.icon = [NSImage imageNamed:@"keyboard-dim"];

		[NSNotificationCenter.defaultCenter addObserver:self
															selector:@selector(dockDidConnectKeyboard:)
																 name:kDockDidConnectKeyboardNotification
															  object:self.view.window.windowController.document];
		if (gNCNub.operationInProgress != kKeyboardActivity)
			[gNCNub requestKeyboard:self];
	} else {
		self.instructions = @"Not connected.";
		self.icon = [NSImage imageNamed:@"keyboard-dim"];
	}

}


/* -----------------------------------------------------------------------------
	Hide the keyboard info panel and cancel the keyboard passthrough protocol.
----------------------------------------------------------------------------- */

- (void)viewWillDisappear {
	if (isRegisteredForDraggedTypes)
		[self.view unregisterDraggedTypes];
	if (gNCNub.isTethered) {
		if (lastResponder) {
			[self.passthruView.window makeFirstResponder:lastResponder];
		}
		[NSNotificationCenter.defaultCenter removeObserver:self];
		if (gNCNub.operationInProgress == kKeyboardActivity) {
			[gNCNub cancelOperation];
		}
	}
	[super viewWillDisappear];
}


/* -----------------------------------------------------------------------------
	Our request for keyboard passthrough was accepted; update the UI.
----------------------------------------------------------------------------- */

- (void)dockDidConnectKeyboard:(NSNotification *)inNotification {

	// passthrough established - update UI
	self.instructions = @"Set an insertion point on your Newton device and start typing.";
	self.icon = [NSImage imageNamed:@"keyboard"];

	lastResponder = self.view.window.firstResponder;
	[self.passthruView.window makeFirstResponder:self.passthruView];
}


/*------------------------------------------------------------------------------
	Notification from the NCPassthruView that the user entered some text.
	Args:		inText
	Return:	--
------------------------------------------------------------------------------*/

- (void)passthruText:(NSString *)inText {
	[gNCNub sendKeyboardText:inText state:0];
}


/*------------------------------------------------------------------------------
	Indicate that our view accepts text to be passed through.
	Args:		sender
	Return:	our willingness to accept the drag
------------------------------------------------------------------------------*/

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
	NSPasteboard * pasteboard = sender.draggingPasteboard;
	NSArray * classes = @[NSString.class];	// also NSURL.class?
	NSDictionary * options = @{ };
//	NSDictionary * options = @{ NSPasteboardURLReadingFileURLsOnlyKey: [NSNumber numberWithBool:YES],
//										 NSPasteboardURLReadingContentsConformToTypesKey: @[kUTTypePlainText] };
	if ([pasteboard canReadObjectForClasses:classes options:options])
		return NSDragOperationCopy;
	return NSDragOperationNone;
}


/*------------------------------------------------------------------------------
	If text was dropped, pass it through as keyboard data.
	Args:		sender
	Return:	YES always
------------------------------------------------------------------------------*/

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
	NSPasteboard * pasteboard = sender.draggingPasteboard;
	NSArray * classes = @[NSString.class];
	NSDictionary * options = @{ };
	NSArray * items = [pasteboard readObjectsForClasses:classes options:options];
	if (items) {
		for (NSString * text in items) {
			[self passthruText:text];
		}
	}
/*
	classes = @[NSURL.class];
	options = @{ NSPasteboardURLReadingFileURLsOnlyKey: [NSNumber numberWithBool:YES],
					 NSPasteboardURLReadingContentsConformToTypesKey: @[kUTTypePlainText] };
	items = [pasteboard readObjectsForClasses:classes options:options];
	if (items) {
		for (NSURL * url in items) {
		// NSString * text = contents of URL;
			[self passthruText:text];
		}
	}
*/
	return YES;
}

@end
