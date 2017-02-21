/*
	File:		CRC.m

	Contains:	CRC16 implementation (used by framed async serial communications tools).

	Written by:	Newton Research Group, 2009.
*/

#include "CRC.h"


/*--------------------------------------------------------------------------------
	CRC16
--------------------------------------------------------------------------------*/

static const unsigned short kCRC16LoTable[16] =
{
	0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
	0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440
};

static const unsigned short kCRC16HiTable[16] =
{
	0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
	0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400
};


@implementation CRC16

/*--------------------------------------------------------------------------------
	Initialize.
	Args:		--
	Return:	--
--------------------------------------------------------------------------------*/

- (id) init
{
	if (self = [super init])
		[self reset];
	return self;
}


/*--------------------------------------------------------------------------------
	Reset the CRC.
	Args:		--
	Return:	--
--------------------------------------------------------------------------------*/

- (void) reset
{
	workingCRC = 0;
}


/*--------------------------------------------------------------------------------
	Add character into CRC computation.
	Args:		inChar
	Return:	--
--------------------------------------------------------------------------------*/

- (void) computeCRC: (unsigned char) inChar
{
	uint32_t index = ((workingCRC & 0xFF) ^ inChar);
	uint32_t loCRC = kCRC16LoTable[index & 0x0F];
	uint32_t hiCRC = kCRC16HiTable[(index & 0xF0) >> 4];
	workingCRC = (workingCRC >> 8) ^ (hiCRC ^ loCRC);
}


/*--------------------------------------------------------------------------------
	Add characters in buffer into CRC computation.
	Args:		inData
				inSize
	Return:	--
--------------------------------------------------------------------------------*/

- (void) computeCRC: (unsigned char *) inData length: (unsigned int) inSize
{
	for ( ; inSize > 0; inSize--)
	{
		uint32_t index = ((workingCRC & 0xFF) ^ *inData++);
		uint32_t loCRC = kCRC16LoTable[index & 0x0F];
		uint32_t hiCRC = kCRC16HiTable[(index & 0xF0) >> 4];
		workingCRC = (workingCRC >> 8) ^ (hiCRC ^ loCRC);
	}
}


/*--------------------------------------------------------------------------------
	Copy 16-bit CRC into two chars in network byte order.
	Args:		--
	Return:	--
--------------------------------------------------------------------------------*/

- (unsigned char)	get: (unsigned int) index;
{
	unsigned char crc16[2];
#if defined(hasByteSwapping)
	crc16[1] = workingCRC;
	crc16[0] = workingCRC >> 8;
#else
	crc16[0] = workingCRC;
	crc16[1] = workingCRC >> 8;
#endif
	return crc16[index];
}

@end
