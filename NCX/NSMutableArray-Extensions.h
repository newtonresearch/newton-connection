/*
	File:		NSMutableArray-Extensions.h

	Contains:	NSMutableArray support category declarations for Newton Connection.

	Written by:	Newton Research Group, 2007.
*/

#import <Foundation/Foundation.h>


@interface NSMutableArray (NCXExtensions)
- (void) put: (id) inItem;
- (id) get;
@end

