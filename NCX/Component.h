/*
	File:		Component.h

	Contains:	Declarations for the NCX component controller.

	Written by:	Newton Research, 2009.
*/

#import <Cocoa/Cocoa.h>
#import "NewtonKit.h"

@class NCDockProtocolController, NCSession;

/* -----------------------------------------------------------------------------
	N C C o m p o n e n t
	A component encapsulates the event handlers for a part of the dock protocol.
	For example, a keyboard component might handle only those events relevant
	to keyboard passthrough.
----------------------------------------------------------------------------- */

@protocol NCComponentProtocol
- (NSArray *)eventTags;
- (NSProgress *)setupProgress;
@end


@interface NCComponent : NSObject<NCComponentProtocol>

@property(nonatomic,weak) NCDockProtocolController * dock;
@property(nonatomic,strong) NSProgress * progress;

- (id)initWithProtocolController:(NCDockProtocolController *)inController;
- (NSArray *)eventTags;
@end
