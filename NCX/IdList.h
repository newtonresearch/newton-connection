/*
	File:		IdList.h

	Contains:	Declarations for the NCIdList set.

	Written by:	Newton Research, 2012.
*/

#import <Cocoa/Cocoa.h>

/* -----------------------------------------------------------------------------
	N C I d L i s t
	A set of soup entry ids created from the encoded/compressed list sent in
	response to the kDBackupSoup command.
----------------------------------------------------------------------------- */

@interface NCIdList : NSObject
{
	NSUInteger	baseId;
	NSUInteger	runBaseId;
	NSMutableIndexSet *	ids;
};
@property (readonly) NSMutableIndexSet * ids;

- (id)	init;
- (void)	setBaseId: (NSUInteger) inId;
- (void)	addId: (NSUInteger) inId;
- (BOOL)	add: (short) inId;
@end
