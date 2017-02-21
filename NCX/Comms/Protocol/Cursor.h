/*
	File:		Cursor.h

	Contains:	Definition of Newton Connection cursors.

	Written by:	Newton Research Group.
*/

#import "Session.h"

@interface NCCursor : NSObject
{
	int cursorId;
	NCSession *__unsafe_unretained session;
}

- (id)	init: (id) inSession;
- (void)	dealloc;
- (Ref)	commonCode: (EventType) inCommand reset: (BOOL) inReset;

- (void)	query: (RefArg) inSoup spec: (RefArg) inSpec;
- (unsigned int)	countEntries;
- (Ref)	gotoKey: (RefArg) inKey;
- (Ref)	move: (int) inOffset;
- (Ref)	entry;
- (Ref)	next;
- (Ref)	prev;
- (Ref)	reset;
- (Ref)	resetToEnd;
@end
