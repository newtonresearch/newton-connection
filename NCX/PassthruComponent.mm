/*
	File:		PassthruComponent.mm

	Contains:	The NCX keyboard passthrough controller.
					Don’t believe we need to wrap session messages in newton_try/end_try
					b/c they’re only sends, and disconnection exceptions are thrown only
					on receive. So disconnection will be detected by the dock eevent loop.

	Written by:	Newton Research, 2009.
*/

#import "PassthruComponent.h"
#import "NCDockProtocolController.h"


@implementation NCPassthruComponent

/*------------------------------------------------------------------------------
	K e y b o a r d   P a s s t h r o u g h
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
	Return the event ids handled by this component.
	Args:		--
	Return:	array of strings
------------------------------------------------------------------------------*/

- (NSArray *)eventTags {
	return [NSArray arrayWithObjects:@"KYBD",	// kDDoRequestKeyboardPassthrough -- start keyboard passthrough
												@"KBDC",	// kDDoKeyboardChar - pass char through
												@"KBDS",	// kDDoKeyboardString - pass string through
												@"CAKY",	// kDDoCancelKeyboardPassthrough
												@"kybd",	// kDStartKeyboardPassthrough
												nil ];
}


/*------------------------------------------------------------------------------
	N e w t o n   D o c k   I n t e r f a c e
------------------------------------------------------------------------------*/
#pragma mark Desktop Event Handlers

/*------------------------------------------------------------------------------
	Desktop requests keyboard passthrough.
	Keyboard passthrough can be started from either end in the same way,
	so send the request on to the Newton.
				kDDoRequestKeyboardPassthrough
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_KYBD:(NCDockEvent *)inEvent {
	[self.dock setDesktopControl:YES];
	[self.dock.session sendEvent:kDStartKeyboardPassthrough];
}


/*------------------------------------------------------------------------------
	Desktop wants to pass text through to Newton.
				kDDoKeyboardChar
				kDDoKeyboardString
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_KBDC:(NCDockEvent *)inEvent {
	[self.dock.session sendEvent:kDKeyboardChar value:inEvent.value];
}

- (void)do_KBDS:(NCDockEvent *)inEvent {
	[self.dock.session sendEvent:kDKeyboardString data:inEvent.data length:inEvent.dataLength];
}


/*------------------------------------------------------------------------------
	Desktop wants to cancel keyboard passthrough.
				kDDoCancelKeyboardPassthrough
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_CAKY:(NCDockEvent *)inEvent {
	[self.dock setDesktopControl:NO];		// will also restore endpoint timeout
}


#pragma mark Newton Event Handlers
/*------------------------------------------------------------------------------
	Newton requests keyboard passthrough.
				kDStartKeyboardPassthrough
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_kybd:(NCDockEvent *)inEvent {
	[self.dock keyboardActivated];
}

@end
