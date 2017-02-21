/*
	File:		DockEvent.h

	Contains:	Newton Dock event interface.
					Commands are exchanged as events (described in Newton/Events.h)
						class	= 'newt' (is it ever anything else?)
						id		= 'dock'

	Written by:	Newton Research Group, 2011.
*/

#import <dispatch/dispatch.h>

#import "NSMutableArray-Extensions.h"
#import "DockProtocol.h"
#import "NewtonKit.h"
#import "Endpoint.h"


/* --- Event send progress callback --- */

typedef void (^NCProgressCallback)(unsigned int totalAmount, unsigned int amountDone);

/* -----------------------------------------------------------------------------
	N C D o c k E v e n t
----------------------------------------------------------------------------- */

#define kEventBufSize	240

@interface NCDockEvent : NSObject

@property(readonly)	NSString * command;
@property(readonly)	EventType tag;
@property(assign)		unsigned int length;
@property(assign)		unsigned int dataLength;
@property(assign)		void * data;
@property(assign)		int value;
@property				Ref ref;
@property(readonly)	Ref ref1;
@property(readonly)	Ref ref2;
@property(copy)		NSURL * file;

+ (NCDockEvent *)makeEvent:(EventType)inCmd;
+ (NCDockEvent *)makeEvent:(EventType)inCmd value:(int)inValue;
+ (NCDockEvent *)makeEvent:(EventType)inCmd ref:(RefArg)inValue;
+ (NCDockEvent *)makeEvent:(EventType)inCmd file:(NSURL *)inURL;
+ (NCDockEvent *)makeEvent:(EventType)inCmd length:(unsigned int)inLength data:(const void *)inData length:(unsigned int)inDataLength;

- (id)initEvent:(EventType)inCmd;
- (NCError)build:(CChunkBuffer *)inData;
- (void)addIndeterminateData:(unsigned char)inData;

- (NewtonErr)send:(NCEndpoint *)ep;
- (NewtonErr)send:(NCEndpoint *)ep callback:(NCProgressCallback)inCallback frequency:(unsigned int)inFrequency;

@end

