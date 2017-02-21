/*
	File:		ROMComponent.h

	Contains:	Declarations for the NCX ROM dump controller.

	Written by:	Newton Research, 2015.
*/

#import "Component.h"

// enable ROM dump functions
#define forROMDump 1

// Desktop events
// Events are initiated by UI actions
// and passed via the document’s NCDockProtocolController.
#define kDDoDumpROM			'DUMP'


@interface NCROMDumpComponent : NCComponent
{
	BOOL isROMDumpExtensionInstalled;
	NSMutableData * romData;
	uint32_t dumpAddr;
	uint32_t dumpSize;
}
- (NSUInteger) dumpPage: (uint32_t) inAddr;
@end
