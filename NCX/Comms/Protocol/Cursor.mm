/*
	File:		Cursor.mm

	Contains:	Implementation of Newton Connection cursors.

	Written by:	Newton Research Group.
*/

#import "Cursor.h"

/*------------------------------------------------------------------------------
	N C C u r s o r
------------------------------------------------------------------------------*/

@implementation NCCursor

/*------------------------------------------------------------------------------
	Initialize the cursor with a reference to its session.
------------------------------------------------------------------------------*/

- (id) init: (id) inSession
{
	if (self = [super init])
	{
		session = (NCSession *) inSession;
		cursorId = 0;
	}
	return self;
}


/*------------------------------------------------------------------------------
	Dispose the cursor.
------------------------------------------------------------------------------*/

- (void) dealloc
{
	[session sendEvent: kDCursorFree value: cursorId];
	[session receiveResult];
}


/*------------------------------------------------------------------------------
	Query a soup.
------------------------------------------------------------------------------*/

- (void) query: (RefArg) inSoupName spec: (RefArg) inSpec
{
	RefVar param(AllocateFrame());

	if (NOTNIL(inSoupName))
		SetFrameSlot(param, SYMA(soupName), inSoupName);
	SetFrameSlot(param, SYMA(querySpec), inSpec);

	[session sendEvent: kDQuery ref: param];
	NCDockEvent * evt = [session receiveEvent: kDLongData];
	cursorId = evt.value;
}


/*------------------------------------------------------------------------------
	Count the number of entries in the cursor.
------------------------------------------------------------------------------*/

- (unsigned int) countEntries
{
	[session sendEvent: kDCursorCountEntries value: cursorId];
	NCDockEvent * evt = [session receiveEvent: kDLongData];
	return evt.value;
}


/*------------------------------------------------------------------------------
	Go to a key.
------------------------------------------------------------------------------*/

- (Ref) gotoKey: (RefArg) inKey
{
	unsigned int keySize = (unsigned int)FlattenRefSize(inKey);
	unsigned int numOfBytes = sizeof(int32_t) + keySize;
	CPtrPipe pipe;

	// parms are
	//		long		cursor id
	//		Ref		key
	char * parms = (char *) malloc(numOfBytes);
	*(int32_t *)parms = CANONICAL_LONG(cursorId);
	pipe.init(parms + sizeof(int32_t), keySize, NO, nil);
	FlattenRef(inKey, pipe);
	[session sendEvent: kDCursorGotoKey data: parms length: numOfBytes];
	free(parms);

	NCDockEvent * evt = [session receiveEvent: kDRefResult];
	return evt.ref;
}


/*------------------------------------------------------------------------------
	Move relative.
------------------------------------------------------------------------------*/

- (Ref) move: (int) inOffset
{
	int32_t parms[2];

	// parms are
	//		long		cursor id
	//		long		offset
	parms[0] = CANONICAL_LONG(cursorId);
	parms[1] = CANONICAL_LONG(inOffset);
	[session sendEvent: kDCursorMove data: parms length: sizeof(parms)];
	NCDockEvent * evt = [session receiveEvent: kDRefResult];
	return evt.ref;
}


/*------------------------------------------------------------------------------
	Return the current entry.
------------------------------------------------------------------------------*/

- (Ref) entry
{
	return [self commonCode: kDCursorEntry reset: NO];
}


/*------------------------------------------------------------------------------
	Return the next entry.
------------------------------------------------------------------------------*/

- (Ref) next
{
	return [self commonCode: kDCursorNext reset: NO];
}


/*------------------------------------------------------------------------------
	Return the previous entry.
------------------------------------------------------------------------------*/

- (Ref) prev
{
	return [self commonCode: kDCursorPrev reset: NO];
}


/*------------------------------------------------------------------------------
	Reset the cursor to the beginning.
------------------------------------------------------------------------------*/

- (Ref) reset
{
	return [self commonCode: kDCursorReset reset: YES];
}


/*------------------------------------------------------------------------------
	Reset the cursor to the end.
------------------------------------------------------------------------------*/

- (Ref) resetToEnd
{
	return [self commonCode: kDCursorResetToEnd reset: YES];
}


/*------------------------------------------------------------------------------
	Perform transaction common to entry retrieval methods.
------------------------------------------------------------------------------*/

- (Ref) commonCode: (EventType) inCommand reset: (BOOL) inReset
{
	[session sendEvent: inCommand value: cursorId];
	if (inReset)
	{
		[session receiveEvent: kDRefResult];
		[session sendEvent: kDCursorEntry value: cursorId];
	}
	NCDockEvent * evt = [session receiveEvent: kDRefResult];
	return evt.ref;
}


@end
