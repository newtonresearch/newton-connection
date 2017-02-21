/*
	File:		ScreenshotComponent.h

	Contains:	Declarations for the NCX screenshot controller.

	Written by:	Newton Research, 2009.
*/

#import "Component.h"


// Desktop events
// Events are initiated by UI actions in the ScreenshotInfoController
// and passed via the document’s NCDockProtocolController.
#define kDDoRequestScreenCapture	'SCRN'
#define kDDoTakeScreenshot			'SNAP'
#define kDDoCancelScreenCapture	'NSCR'


@interface NCScreenshotComponent : NCComponent
{
	BOOL isScreenshotExtensionInstalled;
}
@end
