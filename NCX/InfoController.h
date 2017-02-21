/*
	File:		InfoController.h

	Abstract:	Interface for NCInfoController class.

	Written by:		Newton Research, 2011.
*/

#import <AppKit/AppKit.h>
#import "NCSourceItem.h"
#import "NCDockProtocolController.h"


/* -----------------------------------------------------------------------------
	N C I n f o C o n t r o l l e r
	Controller for the info pane.
	Just as the WindowController knows the document it’s working on, we need
	to know that too.
----------------------------------------------------------------------------- */
@class NCDocument;

@interface NCInfoController : NSViewController
{
	BOOL isRegisteredForDraggedTypes;
}
@property(strong) NCDocument * document;

@end
