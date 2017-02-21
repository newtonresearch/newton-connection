/*
	File:		ScreenshotViewController.m

	Abstract:	Implementation of NCScreenshotInfo and NCScreenshotViewController.

	Written by:		Newton Research, 2011.
*/

#import <CoreServices/CoreServices.h>
#import "ScreenshotViewController.h"
#import "NCDocument.h"
#import "NCWindowController.h"


/* -----------------------------------------------------------------------------
	N C S c r e e n s h o t I n f o
	We just need the class.
----------------------------------------------------------------------------- */

@implementation NCScreenshotInfo
- (NSString *) name {
	return @"Display";
}

- (NSImage *) image {
	return [NSImage imageNamed:@"source-display.png"];
}

- (NSString *) identifier {
	return @"Screenshot";
}

@end


#pragma mark -
/* -----------------------------------------------------------------------------
	N C S c r e e n s h o t V i e w C o n t r o l l e r
	Controller for the screenshot info pane.
	initially: say "Checking Newton device..." w/ progress; listen for click actions; install extensions; call extension
	result = OK	? say "Compose the screen on your Newton device and click the camera to take a screen shot." w/ icon
					: offer to load Toolkit package
	click: say "Receiving screen image..." w/ progress
	received: fill image well (binding)
	view changed: cancel transaction protocol
----------------------------------------------------------------------------- */

@implementation NCScreenshotViewController

/* -----------------------------------------------------------------------------
	Show the screenshot info panel and start the screenshot protocol.
----------------------------------------------------------------------------- */

- (void)viewWillAppear
{
	[super viewWillAppear];

	// rotate screenshot view to match orientation of tethered device
	// even if we’re not tethered now, the document must have a deviceObj
	screenImage.screenSize = self.document.deviceObj.screenSize;

	self.canTakeTheShot = NO;
	if (gNCNub.isTethered) {
		// establish screenshot capability w/ session
		self.instructions = @"Please wait…";
		self.icon = [NSImage imageNamed:@"imageCapture-dim"];

		[NSNotificationCenter.defaultCenter addObserver:self
															selector:@selector(dockDidScreenshot:)
																 name:kDockDidScreenshotNotification
															  object:self.view.window.windowController.document];
		[gNCNub requestScreenshot];
	} else {
		self.instructions = @"Not connected.";
		self.icon = [NSImage imageNamed:@"imageCapture-dim"];
	}

}


/* -----------------------------------------------------------------------------
	Hide the screenshot info panel: cancel the screenshot protocol.
----------------------------------------------------------------------------- */

- (void)viewWillDisappear
{
	if (gNCNub.isTethered) {
		[NSNotificationCenter.defaultCenter removeObserver:self];
		[gNCNub cancelOperation];
	}
	[super viewWillDisappear];
}


/* -----------------------------------------------------------------------------
	Our request for screenshot mode was accepted; update the UI.
----------------------------------------------------------------------------- */

- (void)dockDidScreenshot:(NSNotification *)inNotification
{
	self.instructions = @"Compose the screen on your Newton device.";
	self.icon = [NSImage imageNamed:@"imageCapture"];
	self.canTakeTheShot = YES;
}


/* -----------------------------------------------------------------------------
	The screenshot button was pressed; update the UI and do the protocol.
----------------------------------------------------------------------------- */

- (IBAction)sayCheese:(id)sender
{
	self.instructions = @"Please wait…";
	self.icon = [NSImage imageNamed:@"imageCapture-dim"];
	self.canTakeTheShot = NO;

	[gNCNub takeScreenshot];
// the received image is saved in document.screenshot which is bound to the UI
}


@end


#pragma mark -
@implementation NCScreenshotView

@synthesize screenSize;

- (NSSize)intrinsicContentSize {
	return self.screenSize;
}


#pragma mark - Drag source operations
/* -----------------------------------------------------------------------------
	Catch mouse down events in order to start drag.
----------------------------------------------------------------------------- */

- (void)mouseDown:(NSEvent *)inEvent
{
	if (self.image != nil) {
		/*	Dragging operations occur within the context of a special pasteboard (NSDragPboard).
			All items written or read from a pasteboard must conform to NSPasteboardWriting or NSPasteboardReading respectively.
			NSPasteboardItem implements both these protocols and is a container for any object that can be serialized to NSData. */

		NSPasteboardItem * pbItem = [NSPasteboardItem new];
		/*	Our pasteboard item will support public.tiff representations of our data (the image).
			Rather than compute this representation now, promise that we will provide this representation when asked.
			When a receiver wants our data in one of the above representations, we'll get a call to the
			NSPasteboardItemDataProvider protocol method –pasteboard:item:provideDataForType:. */
		[pbItem setDataProvider:self forTypes:[NSArray arrayWithObjects:NSPasteboardTypeTIFF,NSPasteboardTypePDF,kPasteboardTypeFilePromiseContent,kPasteboardTypeFileURLPromise,nil]];

		/* Create a new NSDraggingItem with our pasteboard item. */
		NSDraggingItem * dragItem = [[NSDraggingItem alloc] initWithPasteboardWriter:pbItem];

		/*	The coordinates of the dragging frame are relative to our view.
			Setting them to our view's bounds will cause the drag image to be the same size as our view.
			Alternatively, you can set the draggingFrame to an NSRect that is the size of the image in the view
			but this can cause the dragged image to not line up with the mouse
			if the actual image is smaller than the size of the our view. */
		NSRect draggingRect = self.bounds;

		/*	While our dragging item is represented by an image, this image can be made up of multiple images
			which are automatically composited together in painting order.
			However, since we are only dragging a single item composed of a single image, we can use the convenience method below. */
		[dragItem setDraggingFrame:draggingRect contents:self.image];

		/* Create a dragging session with our drag item and ourself as the source. */
		NSDraggingSession * draggingSession = [self beginDraggingSessionWithItems:[NSArray arrayWithObject:dragItem] event:inEvent source:self];
		draggingSession.animatesToStartingPositionsOnCancelOrFail = YES;	// cause the dragging item to slide back to the source if the drag fails
		draggingSession.draggingFormation = NSDraggingFormationNone;
	}
}


/* -----------------------------------------------------------------------------
	NSDraggingSource protocol method.
	Return the types of operations allowed in a certain context.
----------------------------------------------------------------------------- */

- (NSDragOperation)draggingSession:(NSDraggingSession *)session sourceOperationMaskForDraggingContext:(NSDraggingContext)context
{
	switch (context) {
	case NSDraggingContextOutsideApplication:
		return NSDragOperationCopy;

	case NSDraggingContextWithinApplication:
	//by using this fall through pattern, we will remain compatible if the contexts get more precise in the future.
	default:
		return NSDragOperationNone;
	}
}


/* -----------------------------------------------------------------------------
	Accept activation click as click in window,
	so source doesn't have to be the active window.
----------------------------------------------------------------------------- */

- (BOOL)acceptsFirstMouse:(NSEvent *)event
{
    return YES;
}


/* -----------------------------------------------------------------------------
	Method called by pasteboard to support promised drag types.
	Sender has accepted the drag and now we need to send the data for the type
	we promised.
----------------------------------------------------------------------------- */

- (void)pasteboard:(NSPasteboard *)inPasteboard item:(NSPasteboardItem *)inItem provideDataForType:(NSString *)inType
{
//NSLog(@"-pasteboard:item:provideDataForType:%@",inType);
	if ([inType compare:NSPasteboardTypeTIFF] == NSOrderedSame) {
		//set data for TIFF type on the pasteboard as requested
	  [inPasteboard setData:[self.image TIFFRepresentation] forType:NSPasteboardTypeTIFF];
	} else if ([inType compare:NSPasteboardTypePDF] == NSOrderedSame) {
		//set data for PDF type on the pasteboard as requested
		[inPasteboard setData:[self dataWithPDFInsideRect:self.bounds] forType:NSPasteboardTypePDF];
	} else if ([inType compare:(__bridge NSString *)kPasteboardTypeFilePromiseContent] == NSOrderedSame) {
		// pasteboard asks for type of file we will generate
		[inPasteboard setString:(__bridge NSString *)kUTTypeTIFF forType:(__bridge NSString *)kPasteboardTypeFilePromiseContent];
	} else if ([inType compare:(__bridge NSString *)kPasteboardTypeFileURLPromise] == NSOrderedSame) {
		// pasteboard asks for promised file
		// we will never see this; file is created in -namesOfPromisedFilesDroppedAtDestination:
NSLog(@"-pasteboard:item:provideDataForType:%@",inType);
	}
}


/* -----------------------------------------------------------------------------
	Promise files for the dragged items.
	To promise URLs in 10.6:
	  NCEntrys can be added to the pasteboard
	  they provide kPasteboardTypeFileURLPromise (the promise)
				  and kPasteboardTypeFilePromiseContent (UTI of file)
----------------------------------------------------------------------------- */

- (NSArray *)namesOfPromisedFilesDroppedAtDestination:(NSURL *)inDestination
{
	NSData * tiffData = [self.image TIFFRepresentation];
	NSString * filename = @"Screenshot.tiff";
	for (int seq = 1; seq < 1000; ++seq) {
		NSURL * docURL = [inDestination URLByAppendingPathComponent:filename];
		NSError __autoreleasing * err = nil;
		if ([tiffData writeToURL:docURL options:NSDataWritingWithoutOverwriting error:&err]) {
			NSDictionary * fileAttrs = @{ NSFileExtensionHidden:[NSNumber numberWithBool:YES] };
			[[NSFileManager defaultManager] setAttributes:fileAttrs ofItemAtPath:docURL.path error:&err];
			break;
		}
		filename = [NSString stringWithFormat:@"Screenshot-%d.tiff",seq];
	}
	return [NSArray arrayWithObject:filename];
}

@end
