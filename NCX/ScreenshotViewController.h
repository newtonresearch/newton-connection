/*
	File:		ScreenshotViewController.h

	Abstract:	Interface for NCScreenshotViewController class.

	Written by:		Newton Research, 2011.
*/

#import "InfoController.h"


/* -----------------------------------------------------------------------------
	N C S c r e e n s h o t I n f o
	The screenshot represented in the source list.
----------------------------------------------------------------------------- */

@interface NCScreenshotInfo : NSObject<NCSourceItem>
@end


/* -----------------------------------------------------------------------------
	N C S c r e e n s h o t V i e w
	The image view that shows the screenshot.
	We need to set its size to match the orientation of the tethered Newton screen
	so we can return its intrinsic size for auto-layout.
	This view also handles dragging of the screenshot out into another app.
----------------------------------------------------------------------------- */
@interface NCScreenshotView : NSImageView<NSDraggingSource, NSPasteboardItemDataProvider>
@property(nonatomic,assign) NSSize screenSize;
@end


/* -----------------------------------------------------------------------------
	N C S c r e e n s h o t V i e w C o n t r o l l e r
	Controller for the screenshot info pane.
----------------------------------------------------------------------------- */

@interface NCScreenshotViewController : NCInfoController
{
	IBOutlet NCScreenshotView * screenImage;
}
@property(nonatomic) NSString * instructions;
@property(nonatomic) NSImage * icon;
@property(nonatomic,assign) BOOL canTakeTheShot;

- (IBAction) sayCheese: (id) sender;

@end


