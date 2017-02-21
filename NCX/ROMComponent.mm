/*
	File:		ROMComponent.mm

	Contains:	Implementation of the NCX ROM dump component.
					Fetch a page of Newton memory (typically the ROM).

	Written by:	Newton Research, 2015.
*/

#import "ROMComponent.h"
#import "NCDockProtocolController.h"
#import "NCDocument.h"
#import "PreferenceKeys.h"


@implementation NCROMDumpComponent

/*------------------------------------------------------------------------------
	Initialize ivars for new connection session.
	Args:		inSession
	Return:	--
------------------------------------------------------------------------------*/

- (id)initWithProtocolController:(NCDockProtocolController *)inController {
	if (self = [super initWithProtocolController: inController]) {
		isROMDumpExtensionInstalled = NO;
	}
	return self;
}


/*------------------------------------------------------------------------------
	Return the event command tags handled by this component.
	Args:		--
	Return:	array of strings
------------------------------------------------------------------------------*/

- (NSArray *)eventTags {
	return [NSArray arrayWithObject:@"DUMP"]; 	// kDDoDumpROM
}


/*------------------------------------------------------------------------------
	If we haven’t loaded the screenshot extensions this session
	then load it now.
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/

- (void)installROMDumpExtensions {
	if (!isROMDumpExtensionInstalled) {
		[self.dock.session loadExtension:@"reqp"];
		isROMDumpExtensionInstalled = YES;
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

- (NSProgress *)setupProgress {
	self.progress = [NSProgress progressWithTotalUnitCount:[[NSUserDefaults standardUserDefaults] integerForKey:kROMSizePref]];
	self.progress.localizedDescription = @"Dumping Newton ROM…";
	return self.progress;
}

/*------------------------------------------------------------------------------
	Set up parameters for ROM dump.
				kDDoDumpROM
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_DUMP:(NCDockEvent *)inEvent {

	[self.dock setDesktopControl:YES];
	[self installROMDumpExtensions];

	NSUserDefaults * defaults = [NSUserDefaults standardUserDefaults];
	dumpAddr = (uint32_t)[defaults integerForKey:kROMAddrPref];
	dumpSize = (uint32_t)[defaults integerForKey:kROMSizePref];

	romData = [[NSMutableData alloc] initWithCapacity:dumpSize];
	
	int64_t totalDumpedSize = 0;

	NSUInteger dumpedSize = 1;	// non-zero to get the iteration started
	while (dumpSize > 0 && dumpedSize > 0) {
		dumpedSize = [self dumpPage:dumpAddr];
		if (dumpedSize) {
			totalDumpedSize += dumpedSize;
			self.progress.completedUnitCount = totalDumpedSize;
			dumpAddr += dumpedSize;
			dumpSize -= dumpedSize;
		}
		if (self.progress.isCancelled)
			break;
	}
	[self.dock setDesktopControl:NO];

	dispatch_async(dispatch_get_main_queue(), ^{
		[self.dock dumpROMDone:romData];
		romData = nil;
	});
}


/*------------------------------------------------------------------------------
	Request a page (4K) of Newton memory.
	Args:		inAddr	virtual address of page
	Return:	size of returned binary object
				0 => error
------------------------------------------------------------------------------*/

- (NSUInteger)dumpPage:(uint32_t)inAddr {
	NSUInteger dumpedSize = 0;
	newton_try
	{
		// Newton will reply with 'page' and binary object if memory read successfully
		NCDockEvent * evt = [self.dock.session callExtension:'reqp' with:inAddr & kRefValueMask];	// makes an NS integer looking like a 32bit-aligned address
		if (evt.tag == 'page') {
			// event ref is a binary object -- should be 4K (or whatever the pext deems fit) if all went well
			CDataPtr chunk(evt.ref);
			dumpedSize = Length(chunk);
			[romData appendBytes:(Ptr)chunk length:dumpedSize];
		}
		// anything else implies an arror
	}
	newton_catch_all
	{
		NewtonErr err = (NewtonErr)(long)CurrentException()->data;
		self.dock.statusText = [NSString stringWithFormat:@"Exception %s (%d) occurred during ROM dump.", CurrentException()->name, err];
		dumpedSize = 0;
	}
	end_try;
	return dumpedSize;
}


@end
