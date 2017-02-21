/*
	File:		IdList.m

	Contains:	Declarations for the NCIdList set.

	Written by:	Newton Research, 2012.
*/

#import "IdList.h"


/* -----------------------------------------------------------------------------
	N C I d L i s t
	A set of soup entry ids created from the encoded/compressed list sent in
	response to the kDBackupSoup command.
----------------------------------------------------------------------------- */

@implementation NCIdList

@synthesize ids;

- (id) init
{
	if (self = [super init])
	{
		baseId = 0;
		runBaseId = 0;
		ids = [NSMutableIndexSet indexSet];
	}
	return self;
}


- (void)	setBaseId: (NSUInteger) inId
{
	baseId = inId;
}


- (void)	addId: (NSUInteger) inId
{
	[ids addIndex: inId];
}


- (BOOL)	add: (short) inId
{
	if (inId == (short)0x8000)
		return NO;

	if (inId < 0)
	{
		int i;
		for (i = 1; i <= -inId; i++)
			[self addId:baseId + runBaseId + i];
	}
	else
	{
		runBaseId = inId;
		[self addId:baseId + inId];
	}
	return YES;
}

@end

