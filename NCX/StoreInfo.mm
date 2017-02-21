/*
	File:		StoreInfo.mm

	Contains:	Newton connection store info model.

	Written by:	Newton Research, 2011.
*/

#import "StoreInfo.h"
#import "Utilities.h"


static NSNumberFormatter * gNumberFormatter;


@implementation NCStoreInfo

+ (void) initialize
{
	gNumberFormatter = [[NSNumberFormatter alloc] init];
	[gNumberFormatter setNumberStyle: NSNumberFormatterDecimalStyle];
}


- (id) initStore: (RefArg) info
{
	if (self = [super initItem: MakeNSString(GetFrameSlot(info, SYMA(name)))])
	{
		int num;
		int totalSize, usedSize;

		kind = [MakeNSString(GetFrameSlot(info, SYMA(kind))) retain];
		// is version really of any interest?
		num = RINT(GetFrameSlot(info, MakeSymbol("storeVersion")));
		version = [[NSString stringWithFormat: @"%d", num] retain];

		totalSize = RINT(GetFrameSlot(info, MakeSymbol("totalSize")));
		capacity = [[NSString stringWithFormat: @"%@K  (%@ bytes)", [gNumberFormatter stringFromNumber: [NSNumber numberWithInt: totalSize / KByte]],
																						[gNumberFormatter stringFromNumber: [NSNumber numberWithInt: totalSize]]] retain];
		usedSize = RINT(GetFrameSlot(info, MakeSymbol("usedSize")));
		used = [[NSString stringWithFormat: @"%@K  (%@ bytes)", [gNumberFormatter stringFromNumber: [NSNumber numberWithInt: usedSize / KByte]],
																				  [gNumberFormatter stringFromNumber: [NSNumber numberWithInt: usedSize]]] retain];
		num = totalSize - usedSize;
		free = [[NSString stringWithFormat: @"%@K  (%@ bytes)", [gNumberFormatter stringFromNumber: [NSNumber numberWithInt: num / KByte]],
																				  [gNumberFormatter stringFromNumber: [NSNumber numberWithInt: num]]] retain];

		status = nil;
		if (NOTNIL(GetFrameSlot(info, MakeSymbol("defaultStore"))))
			status = @"Default store";
		if (NOTNIL(GetFrameSlot(info, MakeSymbol("storePassword"))))
		{
			if (status)
				status = [[NSString stringWithFormat: @"%@, password protected.", status] retain];
			else
				status = @"Password protected.";
		}
		if (status == nil)
			status = @"--";

		isReadOnly = NOTNIL(GetFrameSlot(info, MakeSymbol("readOnly")));

	// select icon in store view
		if ([kind isEqualToString: @"Internal"])
			icon = @"internalStore.tiff";
		else
			icon = @"cardStore.tiff";
		icon = [[[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent: icon] retain];
	}
	return self;
}


- (void) dealloc
{
	[kind release];
	[version release];
	[capacity release];
	[used release];
	[free release];
	[status release];
	[icon release];
	[super dealloc];
}


@end
