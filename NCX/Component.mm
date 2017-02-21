/*
	File:		Component.mm

	Contains:	The NCX component controller.

	Written by:	Newton Research, 2009.
*/

#import "Component.h"
#import "NCDockProtocolController.h"


@implementation NCComponent
/* -----------------------------------------------------------------------------
	N C X C o m p o n e n t
----------------------------------------------------------------------------- */
@synthesize dock;

/*------------------------------------------------------------------------------
	Start a new session with a Newton device.
	Args:		inSession
	Return:	--
------------------------------------------------------------------------------*/

- (id)initWithProtocolController:(NCDockProtocolController *)inController {
	if (self = [super init]) {
		self.dock = inController;
	}
	return self;
}


/*------------------------------------------------------------------------------
	Functions to be overridden.
------------------------------------------------------------------------------*/

- (NSArray *)eventTags {
	return nil;
}

- (NSProgress *)setupProgress {
	return nil;
}

@end
