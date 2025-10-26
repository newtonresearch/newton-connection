/*
	File:		DockEventQueue.mm

	Contains:	Newton Dock event implementation.

	Written by:	Newton Research Group, 2011.
*/

#import "DockEventQueue.h"
#import "DockErrors.h"
#import "Logging.h"


/* -----------------------------------------------------------------------------
	N C D o c k E v e n t Q u e u e
----------------------------------------------------------------------------- */
@interface NCDockEventQueue ()
{
	NCEndpointController * endpointController;

	NCDockEvent * eventUnderConstruction;
	NSMutableArray * eventQueue;
	dispatch_semaphore_t eventReady;
	dispatch_queue_t accessQueue;
}
@end


@implementation NCDockEventQueue

+ (NCDockEventQueue *)sharedQueue {		// required for endpoints to build events w/ read data
	static NCDockEventQueue * theQueue = nil;
	if (theQueue == nil) {
		theQueue = [[NCDockEventQueue alloc] init];
	}
	return theQueue;
}


/*------------------------------------------------------------------------------
	Initialize the queue.
	Create an endpoint controller to listen for data.
	Create an empty event for construction once data arrives, and a semaphore
	to signal when an event has been completely received.
	Args:		--
	Return:	self
------------------------------------------------------------------------------*/

- (id)init {
	if (self = [super init]) {
		eventQueue = [[NSMutableArray alloc] initWithCapacity:2];
		eventUnderConstruction = [[NCDockEvent alloc] init];
		accessQueue = dispatch_queue_create("com.newton.connection.event", NULL);
		endpointController = [[NCEndpointController alloc] init];
	}
	return self;
}

- (void)observeValueForKeyPath:(NSString *)inKeyPath
							 ofObject:(id)inObject
								change:(NSDictionary *)inChange
							  context:(void *)inContext {
	//NSNumber * err = [inChange objectForKey:NSKeyValueChangeNewKey];
	//doesn’t really matter what the error is
	[self addEvent:nil];
}


/*------------------------------------------------------------------------------
	Open the queue -- start listening.
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/

- (void)open {
	eventReady = dispatch_semaphore_create(0);
	[endpointController startListening];
	[endpointController addObserver:self forKeyPath:@"error" options:NSKeyValueObservingOptionNew context:nil];
}


/*------------------------------------------------------------------------------
	Close the queue -- flush events and stop listening for more.
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/

- (void)close {
	if (endpointController.isActive) {
		[endpointController stop];
		endpointController.error = kDockErrAccessDenied;
		[endpointController removeObserver:self forKeyPath:@"error"];
	}
}


/*------------------------------------------------------------------------------
	Dispose the queue.
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/

- (void)dealloc {
	[self close];
	endpointController = nil;
	eventQueue = nil;
	eventUnderConstruction = nil;
	eventReady = nil;
	accessQueue = nil;
}


- (void)suppressEndpointTimeout:(BOOL)inDoSuppress {
	[endpointController suppressTimeout:inDoSuppress];
}


#pragma mark - Queue access
/*------------------------------------------------------------------------------
	Construct an event from data received.
	Once the event has been completely received, signal its readiness.
	Args:		inData
	Return:	--
------------------------------------------------------------------------------*/

- (void)readEvent:(CChunkBuffer *)inData {
	while ([eventUnderConstruction build:inData] == noErr) {
		// queue up the completed event
		[self addEvent:eventUnderConstruction];
		// start building a new event
		eventUnderConstruction = [[NCDockEvent alloc] init];
	}
}


/*------------------------------------------------------------------------------
	Add an event to the queue.
	This can be used for local (desktop) event generation.
	Args:		inCmd
	Return:	--
------------------------------------------------------------------------------*/

- (void)addEvent:(NCDockEvent *)inEvt {
	if (eventReady) {
		dispatch_sync(accessQueue, ^{
			if (inEvt) {
				[eventQueue addObject:inEvt];
			}
			dispatch_semaphore_signal(eventReady);
		});
	}
}


/*------------------------------------------------------------------------------
	Remove an event from the queue.
	Will block on eventReady semaphore.
	Args:		--
	Return:	event object
				caller must explicitly release
------------------------------------------------------------------------------*/

- (NCDockEvent *)getNextEvent {
	NCDockEvent *__block evt = nil;
	if (endpointController.error == noErr) {
		dispatch_semaphore_wait(eventReady, DISPATCH_TIME_FOREVER);
	}
	dispatch_sync(accessQueue, ^{
		if (eventQueue.count > 0) {
			evt = [eventQueue objectAtIndex:0];
			[eventQueue removeObjectAtIndex:0];
		}
	});
	return evt;
}

- (BOOL)isEventReady {
	if (endpointController.error) {
		return YES;
	}

	BOOL __block isReady = NO;
	dispatch_sync(accessQueue, ^{
		if (eventQueue.count > 0) {
			isReady = YES;
		}
	});
	return isReady;
}


#pragma mark - Event Transmission
/*------------------------------------------------------------------------------
	Send an event over the endpoint.
	Args:		inCmd
	Return:	error code
------------------------------------------------------------------------------*/

- (NewtonErr)sendEvent:(EventType)inCmd {
	NewtonErr err = endpointController.error;
	return err ? err : [[NCDockEvent makeEvent:inCmd] send:endpointController.endpoint];
}


- (NewtonErr)sendEvent:(EventType)inCmd value:(int)inValue {
	NewtonErr err = endpointController.error;
	return err ? err : [[NCDockEvent makeEvent:inCmd value:inValue] send:endpointController.endpoint];
}


- (NewtonErr)sendEvent:(EventType)inCmd ref:(RefArg)inRef {
	NewtonErr err = endpointController.error;
	return err ? err : [[NCDockEvent makeEvent:inCmd ref:inRef] send:endpointController.endpoint];
}


- (NewtonErr)sendEvent:(EventType)inCmd file:(NSURL *)inURL callback:(NCProgressCallback)inCallback frequency:(unsigned int)inFrequency {
	NewtonErr err = endpointController.error;
	return err ? err : [[NCDockEvent makeEvent:inCmd file:inURL] send:endpointController.endpoint callback:inCallback frequency:inFrequency];
}


- (NewtonErr)sendEvent:(EventType)inCmd length:(unsigned int)inLength data:(const void *)inData length:(unsigned int)inDataLength {
	NewtonErr err = endpointController.error;
	return err ? err : [[NCDockEvent makeEvent:inCmd length:inLength data:inData length:inDataLength] send:endpointController.endpoint];
}


- (NewtonErr)sendEvent:(EventType)inCmd data:(const void *)inData length:(unsigned int)inLength callback:(NCProgressCallback)inCallback frequency:(unsigned int)inFrequency {
	NewtonErr err = endpointController.error;
	return err ? err : [[NCDockEvent makeEvent:inCmd length:inLength data:inData length:inLength] send:endpointController.endpoint callback:inCallback frequency:inFrequency];
}

@end

