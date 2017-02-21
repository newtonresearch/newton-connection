/*
	File:		NSMutableArray-Extensions.m

	Contains:	NSMutableArray support category declarations for Newton Connection.
					

	Written by:	Newton Research Group, 2007.
*/

#import "NSMutableArray-Extensions.h"


@implementation NSMutableArray (NCXExtensions)

/*------------------------------------------------------------------------------
	Put an item into a FIFO queue.
	Args:		inItem		must not be nil
	Return:	--
------------------------------------------------------------------------------*/

- (void) put: (id) inItem
{
	[self addObject: inItem];
}


/*------------------------------------------------------------------------------
	Get an item from a FIFO queue.
	Args:		--
	Return:	item			must be an item in the queue
------------------------------------------------------------------------------*/

- (id) get
{
	id item = nil;
	if ([self count] > 0)
	{
		item = [self objectAtIndex:0];
		[self removeObjectAtIndex:0];
	}
	return item;
}

@end

