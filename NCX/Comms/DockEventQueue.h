/*
	File:		DockEventQueue.h

	Contains:	Newton Dock event interface.
					Commands are exchanged as events (described in Newton/Events.h)
						class	= 'newt' (is it ever anything else?)
						id		= 'dock'

	Written by:	Newton Research Group, 2011.
*/

#import "DockEvent.h"

/* -----------------------------------------------------------------------------
	N C D o c k E v e n t Q u e u e
----------------------------------------------------------------------------- */

@interface NCDockEventQueue : NSObject

@property(class,readonly) NCDockEventQueue * sharedQueue;
@property(readonly) BOOL isEventReady;

- (void)open;
- (void)close;
- (void)readEvent:(CChunkBuffer *)inData;
- (void)addEvent:(NCDockEvent *)inCmd;
- (NCDockEvent *)getNextEvent;
- (void)suppressEndpointTimeout:(BOOL)inDoSuppress;

- (NewtonErr)sendEvent:(EventType)inCmd;
- (NewtonErr)sendEvent:(EventType)inCmd value:(int)inValue;
- (NewtonErr)sendEvent:(EventType)inCmd ref:(RefArg)inRef;
- (NewtonErr)sendEvent:(EventType)inCmd length:(unsigned int)inLength data:(const void *)inData length:(unsigned int)inDataLength;
- (NewtonErr)sendEvent:(EventType)inCmd data:(const void *)inData length:(unsigned int)inLength callback:(NCProgressCallback)inCallback frequency:(unsigned int)inFrequency;
- (NewtonErr)sendEvent:(EventType)inCmd file:(NSURL *)inURL callback:(NCProgressCallback)inCallback frequency:(unsigned int)inFrequency;
@end

