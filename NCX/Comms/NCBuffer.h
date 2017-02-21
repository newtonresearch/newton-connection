/*
	File:		NCBuffer.h

	Contains:	A simple buffer implementation.

	Written by:	Newton Research Group, 2012.
*/

#import <Cocoa/Cocoa.h>

/* -----------------------------------------------------------------------------
	N C B u f f e r
----------------------------------------------------------------------------- */

#define kPageBufSize 1024


@interface NCBuffer : NSObject
{
	unsigned char	buf[kPageBufSize];
	unsigned int	index;
	unsigned int	lastCount;
}
@property (readonly) unsigned char * ptr;
@property (readonly) unsigned int freeSpace;
@property (readonly) unsigned int usedSpace;
@property (assign) unsigned int count;
@property (assign) int nextChar;

- (void) clear;
- (void) reset;
- (void) mark;
- (void) refill;

- (unsigned int) fill: (unsigned int) inAmount;
- (unsigned int) fill: (unsigned int) inAmount from: (const void *) inBuf;
- (void) drain: (unsigned int) inAmount;
- (unsigned int) drain: (unsigned int) inAmount into: (void *) inBuf;

@end

