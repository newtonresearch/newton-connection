/*
	File:		NCBuffer.m

	Contains:	A simple buffer implementation.

	Written by:	Newton Research Group, 2012.
*/

#import "NCBuffer.h"

/* -----------------------------------------------------------------------------
	N C B u f f e r
	count => limit of data written to buffer
	index => limit of data read from buffer
----------------------------------------------------------------------------- */

@implementation NCBuffer

- (id) init
{
	if (self = [super init])
	{
		[self clear];
	}
	return self;
}


- (void) clear
{ [self reset]; self.count = 0; }


- (void) reset
{ index = 0; }


- (void) mark
{ lastCount = _count; }


- (void) refill
{ [self reset]; _count = lastCount; }


- (unsigned char *) ptr
{ return buf + index; }


- (unsigned int) freeSpace
{ return kPageBufSize - _count; }


- (unsigned int) usedSpace
{ return _count - index; }


- (int) nextChar
{ if (index < _count) return buf[index++]; else {[self clear]; return -1;} }


- (void) setNextChar: (int) inCh
{ if (_count < kPageBufSize) buf[_count++] = inCh; }


// return amount actually filled
- (unsigned int) fill: (unsigned int) inAmount
{
	unsigned int actualAmount = inAmount;
	if (actualAmount > self.freeSpace)
		actualAmount = self.freeSpace;
	_count += actualAmount;
	return actualAmount;
}


- (unsigned int) fill: (unsigned int) inAmount from: (const void *) inBuf
{
	@synchronized(self)
	{
		unsigned int actualAmount = inAmount;
		if (actualAmount > self.freeSpace)
			actualAmount = self.freeSpace;
//NSLog(@"-[NCBuffer fill: %d from: %p] into %p", actualAmount, inBuf, buf+_count);
		memcpy(buf+_count, inBuf, actualAmount);
		return [self fill:actualAmount];
	}
}


- (void) drain: (unsigned int) inAmount
{
	unsigned int actualAmount = inAmount;
	if (actualAmount > self.usedSpace)
		actualAmount = self.usedSpace;
	index += actualAmount;
	if (index == _count)
		[self clear];
}


- (unsigned int) drain: (unsigned int) inAmount into: (void *) inBuf
{
	@synchronized(self)
	{
		unsigned int actualAmount = inAmount;
		if (actualAmount > self.usedSpace)
			actualAmount = self.usedSpace;
//NSLog(@"-[NCBuffer drain: %d into: %p] from %p", actualAmount, inBuf, self.ptr);
//NSLog(@"%@", self.description);
		memcpy(inBuf, self.ptr, actualAmount);
		[self drain:actualAmount];
		return actualAmount;
	}
}


- (NSString *) description
{
	char dbuf[4096];
	int i, len = sprintf(dbuf, "NCBuffer index:%u, count:%u", index,_count);
	if (_count > index)
	{
		len += sprintf(dbuf+len, ", data:");
		char * s = dbuf+len;
		unsigned char * p = self.ptr;
		for (i = index; i < _count; i++, p++, s+=3)
		{
			sprintf(s, " %02X", *p);
		}
		*s = 0;
	}
	return [NSString stringWithUTF8String:dbuf];
}


@end

