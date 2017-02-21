/*
	File:		KeyboardViewController.h

	Abstract:	Interface for NCKeyboardViewController class.

	Written by:		Newton Research, 2011.
*/

#import "InfoController.h"


@interface NCKeyboardInfo : NSObject <NCSourceItem>
@end


/* -----------------------------------------------------------------------------
	N C K e y b o a r d V i e w C o n t r o l l e r
	Controller for the keyboard passthrough info pane.
----------------------------------------------------------------------------- */
#import "NCXPassthruView.h"

@interface NCKeyboardViewController : NCInfoController<NCPassthru>
@property(nonatomic) IBOutlet NSView * passthruView;
@property(nonatomic) NSString * instructions;
@property(nonatomic) NSImage * icon;
@end
