/*
	File:		Newton1Component.h

	Contains:	Declarations for the Newton OS 1 protocol handler.

	Written by:	Newton Research, 2012.
*/

#import "NCDockProtocolController.h"


@interface NCNewton1Component : NCComponent

- (NSString *) appForSoup: (NSString *) inSoupName;
- (Ref) setupDevice;
- (NSIndexSet *) extractIdsFromEvent: (NCDockEvent *) inEvent;

@end
