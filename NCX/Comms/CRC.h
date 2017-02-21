/*
	File:		CRC.h

	Contains:	CRC16 declarations (used by framed async serial communications tools).

	Written by:	Newton Research Group, 2009.
*/

#include <Foundation/Foundation.h>


/*--------------------------------------------------------------------------------
	CRC16
--------------------------------------------------------------------------------*/

@interface CRC16 : NSObject
{
	uint32_t		workingCRC;
};

- (id)	init;

- (void)	reset;
- (void)	computeCRC: (unsigned char) inChar;
- (void)	computeCRC: (unsigned char *) inData length: (unsigned int) inSize;
- (unsigned char)	get: (unsigned int) index;

@end
