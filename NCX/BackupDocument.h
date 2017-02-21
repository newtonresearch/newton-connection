/*
	File:		BackupDocument.h

	Abstract:	NCX backup document.

	Written by:	Newton Research, 2012.
*/

#import "NCDocument.h"


/*------------------------------------------------------------------------------
	B a c k u p F i l e H e a d e r
------------------------------------------------------------------------------*/

struct SourceInfo
{
	uint32_t version;
	uint32_t manufacturer;
	uint32_t machineType;
};

struct BackupFileHeader
{
	char signature[8];
	SourceInfo source;
};


/* -----------------------------------------------------------------------------
	N B D o c u m e n t
	A Newton Backup Document models what’s in an .nbku file
	so it contains
		store info
		a list of the applications and their soups
		a list of orphan soups
	each soup contains a list of all its entries
	so there are potentially many thousands of objects
----------------------------------------------------------------------------- */

@interface NBDocument : NCDocument

- (NCDevice *)makeDevice:(NSString *)inName source:(SourceInfo *)inSource;
- (NCStore *)makeStore:(RefArg)inStoreRef;
- (NCApp *)makeApp:(NSString *)inName;
- (NCSoup *)makeSoup:(NSString *)inName fromData:(CStdIOPipe &)inPipe indexesRange:(NSRange)indexesRange infoRange:(NSRange)infoRange info:(RefArg)info;

@end


@interface NCSoup (forNCX1)
- (NCEntry *)addEntry:(RefArg)inEntry fromData:(CStdIOPipe &)inPipe range:(NSRange)inRange;
@end
