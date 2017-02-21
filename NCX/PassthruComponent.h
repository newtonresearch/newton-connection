/*
	File:		PassthruComponent.h

	Contains:	Declarations for the NCX keyboard passthrough controller.

	Written by:	Newton Research, 2009.
*/

#import "Component.h"


// Desktop events
// Events are initiated by UI actions in the KeyboardInfoController
// and passed via the document’s NCDockProtocolController.
#define kDDoRequestKeyboardPassthrough	'KYBD'
#define kDDoKeyboardChar					'KBDC'
#define kDDoKeyboardString					'KBDS'
#define kDDoCancelKeyboardPassthrough	'CAKY'


@interface NCPassthruComponent : NCComponent
@end
