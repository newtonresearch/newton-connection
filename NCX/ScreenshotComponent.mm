/*
	File:		ScreenshotComponent.mm

	Contains:	Implementation of the NCX screenshot controller.
					Display Newton screen shot in a window and copy it to the pasteboard.

	Written by:	Newton Research, 2009.
*/

#import "ScreenshotComponent.h"
#import "NCDockProtocolController.h"
#import "NCDocument.h"


@implementation NCScreenshotComponent

/*------------------------------------------------------------------------------
	Initialize ivars for new connection session.
	Args:		inSession
	Return:	--
------------------------------------------------------------------------------*/

- (id)initWithProtocolController:(NCDockProtocolController *)inController {
	if (self = [super initWithProtocolController: inController]) {
		isScreenshotExtensionInstalled = NO;
	}
	return self;
}


/*------------------------------------------------------------------------------
	Return the event command tags handled by this component.
	Args:		--
	Return:	array of strings
------------------------------------------------------------------------------*/

- (NSArray *)eventTags
{
	return [NSArray arrayWithObjects:@"SCRN",	// kDDoRequestScreenCapture -- UI screen shot button pressed
												@"SNAP",	// kDDoTakeScreenshot -- UI camera shutter button pressed
												@"NSCR",	// kDDoCancelScreenCapture -- UI cancelled
												nil ];
}


/*------------------------------------------------------------------------------
	If we haven’t loaded the screenshot extensions this session
	then load it now.
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/

- (void)installScreenshotExtensions {
	if (!isScreenshotExtensionInstalled) {
		[self.dock.session loadExtension:@"scrn"];
		[self.dock.session loadExtension:@"snap"];
		isScreenshotExtensionInstalled = YES;
	}
}


/*------------------------------------------------------------------------------
	N e w t o n   D o c k   I n t e r f a c e
------------------------------------------------------------------------------*/

#pragma mark Desktop Event Handlers
/*------------------------------------------------------------------------------
	This is an entirely desktop-driven protocol, so we don’t listen for
	Newton commands.
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
	Request a screen capture session.
				kDDoRequestScreenCapture
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_SCRN:(NCDockEvent *)inEvent {
	newton_try
	{
		[self.dock setDesktopControl: YES];
		[self installScreenshotExtensions];

		// We need to hide the Newton’s Dock icon slip so we can take a shot of the Newton’s screen.
		// This is done thru a protocol extension.
		// Newton will reply with 'dres' == noErr if screenshot is activated,
		// or dres == -28010 if there’s no Toolkit app to be found.
		Ref screenInfo = NILREF;
		NCDockEvent * evt = [self.dock.session callExtension: 'scrn' with: screenInfo];
		[self.dock screenshotActivated:evt];
	}
	newton_catch_all
	{
		NewtonErr err = (NewtonErr)(long)CurrentException()->data;
		self.dock.statusText = [NSString stringWithFormat:@"Exception %s (%d) occurred during screen capture activation.", CurrentException()->name, err];
	//	need to cancel screenshot
	}
	end_try;
}


/*------------------------------------------------------------------------------
	Perform yer actual screen capture.
				kDDoTakeScreenshot
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_SNAP:(NCDockEvent *)inEvent {
	newton_try
	{
		// This is done thru a protocol extension that calls a function in the Toolkit app.
		// Newton will reply with 'shot' and screen shot data,
		// or dres == -28010 if there’s no Toolkit app to be found.
		Ref screenInfo = NILREF;
		NCDockEvent * evt = [self.dock.session callExtension:'snap' with:screenInfo];
		[self.dock screenshotReceived:evt];
	}
	newton_catch_all
	{
		NewtonErr err = (NewtonErr)(long)CurrentException()->data;
		self.dock.statusText = [NSString stringWithFormat:@"Exception %s (%d) occurred during screen capture.\n", CurrentException()->name, err];
	//	need to cancel screenshot
	}
	end_try;
}


/*------------------------------------------------------------------------------
	Cancel screen capture session.
				kDDoCancelScreenCapture
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_NSCR:(NCDockEvent *)inEvent {
	newton_try
	{
		// calling 'scrn' extension toggles screen capture mode
		Ref screenInfo = NILREF;
		/*NCDockEvent * evt =*/ [self.dock.session callExtension:'scrn' with:screenInfo];
		/*NSAssert(evt.tag == 'dres', @"expected kDResult");*/
		[self.dock setDesktopControl:NO];	// will also restore endpoint timeout
	}
	newton_catch_all
	{
		NewtonErr  err = (NewtonErr)(long)CurrentException()->data;
		self.dock.statusText = [NSString stringWithFormat:@"Exception %s (%d) occurred during screen capture cancellation.\n", CurrentException()->name, err];
	//	need to cancel screenshot
	}
	end_try;
}


@end
