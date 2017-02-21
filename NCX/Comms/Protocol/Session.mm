/*
	File:		Session.mm

	Contains:	Implementation of the dock session protocol.

	Written by:	Newton Research Group, 2005.

	Discussion:	The tickle timer sends helo if no activity after 30 seconds.
					In practice, that means if nothing received for 30s, no?
*/

#import "Cursor.h"	// > Session.h
#import "PreferenceKeys.h"
#import "NCXPlugIn.h"
#import "NCXErrors.h"
#import "PlugInUtilities.h"
#import "Newton/PackageParts.h"


/* -----------------------------------------------------------------------------
	Declarations.
----------------------------------------------------------------------------- */
DeclareException(exComm, exRootException);

extern NSString *	MakeNSString(RefArg inStr);

DeclareException(exBadArgs, exRootException);
#define VALIDARG(expr) if (!(expr)) ThrowErr(exBadArgs, kNCInvalidParameter);

// Set a timeout of 1 hour, effectively to stop the tickle timer
#define kNoTimeout 3600ull

#define kMinutes1904to1970 34714080


/*------------------------------------------------------------------------------
	N C S e s s i o n
------------------------------------------------------------------------------*/
@interface NCSession ()
{
//	event queue
	NCDockEventQueue *__weak dockEventQueue;

//	event handlers
	NSMutableDictionary * eventHandlers;
	dispatch_queue_t tickleQ;
   dispatch_source_t tickleTimer;
	BOOL isProtocolActive;
	int tHexade;
	int tDelta;
}
- (void)			waitForEvent;
- (void)			doDockEventLoop;
- (void)			resetTickler:(int64_t)inSeconds;
- (void)			tickle;
@end


@implementation NCSession

@synthesize isProtocolActive;

/*------------------------------------------------------------------------------
	Initialize.
	Set up the endpoint controller that instantiates all available endpoints
	and listens for a connection.
	Set up the event handler subsystem to handle common events: docking and
	desktop file browsing.
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/

- (id)init {
	if (self = [super init]) {
		eventHandlers = [[NSMutableDictionary alloc] initWithCapacity:32];
		tickleQ = nil;
		tickleTimer = nil;
		dockEventQueue = NCDockEventQueue.sharedQueue;
	}
	return self;
}


- (void)open {
	[dockEventQueue open];
	isProtocolActive = NO;
	tHexade = 0;
	tDelta = 0;
	[self waitForEvent];
}


- (void)close {
	// stop the session:
	// signal the dock event queue to return a nil evt when empty
	[self stopTickler];
	[dockEventQueue close];
}


- (void)dealloc {
	[self close];
	eventHandlers = nil;
}


/*------------------------------------------------------------------------------
	Register a dock event handler component.
	Args:		inComponent
	Return:	--
------------------------------------------------------------------------------*/

- (void)registerEventHandler:(id<NCComponentProtocol>)inComponent {
	NSArray * tags = inComponent.eventTags;
	NSAssert(tags != nil && tags.count > 0, @"no event ids to register");
	for (NSString * tag in tags) {
		eventHandlers[tag] = inComponent;	// dictionary retains component
	}
}


/*------------------------------------------------------------------------------
	Connection has been negotiated -- start the session.
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/

- (void)startTickler {
	NCSession *__weak weakself = self;	// use weak reference so handler block does not retain self
	// send HELO if inactive for 30 seconds
	tickleQ = dispatch_queue_create("com.newton.connection.tickle", NULL);
	tickleTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, tickleQ);
	dispatch_source_set_event_handler(tickleTimer, ^{
		[weakself tickle];
	});
	[self resetTickler: kDefaultTimeout];
	dispatch_resume(tickleTimer);
}


- (void)tickle {
	if (!isProtocolActive) {
		[dockEventQueue sendEvent:kDHello];
	}
}


- (void)resetTickler:(int64_t)inSeconds {
	if (tickleTimer) {
		dispatch_source_set_timer(tickleTimer, dispatch_time(DISPATCH_TIME_NOW, inSeconds * NSEC_PER_SEC), inSeconds * NSEC_PER_SEC, 1ull * NSEC_PER_SEC);
	}
}


- (void)stopTickler {
	if (tickleTimer) {
		dispatch_source_cancel(tickleTimer);
	}
	tickleTimer = nil;
	tickleQ = nil;
}


/*------------------------------------------------------------------------------
	Suppress communications timeout.
	We need to do this for keyboard passthrough and screenshot functions, since
	there is no protocol exchange while waiting for user action.
	Args:		inDoSuppress
	Return:	--
 ------------------------------------------------------------------------------*/

- (void)suppressTimeout:(BOOL)inDoSuppress {
	[dockEventQueue suppressEndpointTimeout:inDoSuppress];
}


/*------------------------------------------------------------------------------
	Dispatch a block to the dock queue.
	This waits for a dock event to arrive then despatches it to its registered
	event handler.
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)waitForEvent {
	NCSession *__weak weakself = self;	// use weak reference so async block does not retain self
	dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		[weakself doDockEventLoop];
		// if we get here then the event queue has been flushed, so we can ditch it: there are no more events coming
		// the queue has been disconnected from its data stream
		// and equally importantly, there is no protocol exchange in progress

		dispatch_async(dispatch_get_main_queue(), ^{
			[NSNotificationCenter.defaultCenter postNotificationName:kDockDidDisconnectNotification object:gNCNub userInfo:@{@"error":[NSNumber numberWithInt:kDockErrDisconnected]}];
		});

	});
}

- (void)doDockEventLoop {
	NCDockEvent * evt;
	do {
		isProtocolActive = NO;
		[self resetTickler:kDefaultTimeout];
		evt = [dockEventQueue getNextEvent];
//		switch (evt.tag), or…
		if (evt) {
			NSString * cmd = evt.command;
			id evtHandler = eventHandlers[cmd];
			if (evtHandler) {
				SEL tag = NSSelectorFromString([NSString stringWithFormat:@"do_%@:", cmd]);
				NSAssert([evtHandler respondsToSelector:tag], @"evtHandler does not handle command");
				// stop sending kDHello while transaction in progress
				[self resetTickler:kNoTimeout];
				isProtocolActive = YES;
				[evtHandler performSelector:tag withObject:evt];
			}
		}
	} while (evt);
}

 - (id)eventHandlerFor:(EventType)inCmd {
	NSString * cmd = [NSString stringWithFormat:@"%c%c%c%c", (inCmd >> 24) & 0xFF, (inCmd >> 16) & 0xFF, (inCmd >> 8) & 0xFF, inCmd & 0xFF];
	return eventHandlers[cmd];
}


#pragma mark Event Queuing
/*------------------------------------------------------------------------------
	Create a desktop event and submit it to the dock event queue.
	Desktop events seldom have any associated data so we can create the event
	from just its id.
	Args:		inCmd
	Return:	--
------------------------------------------------------------------------------*/

- (void) doEvent: (EventType) inCmd
{
	NCDockEvent * req = [NCDockEvent makeEvent: inCmd];
	[dockEventQueue addEvent: req];
}

- (void) doEvent: (EventType) inCmd data: (const void *) inData length: (unsigned int) inLength
{
	NCDockEvent * req = [NCDockEvent makeEvent: inCmd length: inLength data: (void *)inData length: inLength];
	[dockEventQueue addEvent: req];
}


/*------------------------------------------------------------------------------
	Send an event over the endpoint.
	Args:		inCmd
				inReply
	Return:	error code
------------------------------------------------------------------------------*/

- (NewtonErr)sendEvent:(EventType)inCmd
{ [self resetTickler:kDefaultTimeout]; return [dockEventQueue sendEvent:inCmd]; }

- (NewtonErr)sendEvent:(EventType)inCmd value:(int)inValue
{ [self resetTickler:kDefaultTimeout]; return [dockEventQueue sendEvent:inCmd value:inValue]; }

- (NewtonErr)sendEvent:(EventType)inCmd ref:(RefArg)inRef
{ [self resetTickler:kDefaultTimeout]; return [dockEventQueue sendEvent:inCmd ref:inRef]; }

- (NewtonErr)sendEvent:(EventType)inCmd data:(const void *)inData length:(unsigned int)inLength
{ [self resetTickler:kDefaultTimeout]; return [dockEventQueue sendEvent:inCmd length:inLength data:inData length:inLength]; }

- (NewtonErr)sendEvent:(EventType)inCmd length:(unsigned int)inLength data:(const void *)inData length:(unsigned int)inDataLength
{ [self resetTickler:kDefaultTimeout]; return [dockEventQueue sendEvent:inCmd length:inLength data:inData length:inDataLength]; }


/*------------------------------------------------------------------------------
	Send an event over the endpoint, and wait for a reply.
	Args:		inCmd
				inReply
	Return:	the reply event

	TO DO:	handle errors
------------------------------------------------------------------------------*/

- (NCDockEvent *)sendEvent:(EventType)inCmd expecting:(EventType)inReply {
	/*NewtonErr err =*/ [self sendEvent:inCmd];
	// handle error?
	return [self receiveEvent:inReply];
}


/*------------------------------------------------------------------------------
	Receive an event with a specified command.
	This must NEVER be called from outside the -waitForEvent context, otherwise
	you’ll deadlock.
	What we COULD do is:
		set the tag we’re waiting for so -waitForEvent can listen out for it
		block here
		-waitForEvent signals us when the event tag is received
	Args:		inCmd				the command we are expecting
									0 => any command will do
	Return:	the reply event

	TO DO:	handle errors too: eg disconnect, timeout
------------------------------------------------------------------------------*/

- (NCDockEvent *) receiveEvent: (EventType) inCmd
{
	NCDockEvent * evt;
	do {
		// -receiveEvent is only ever called within a protocol exchange
		// so we should NOT reset the tickler
		// [self resetTickler: kDefaultTimeout];
		evt = [dockEventQueue getNextEvent];
	} while (evt && evt.tag == kDHello);

	if (evt == nil)
	{
NSLog(@"-[NCSession receiveEvent:] nil event");
		// nil event => event queue has been disconnected from its data stream
		ThrowErr(exComm, kDockErrDisconnected);
	}

	if (inCmd != 0
	&&  inCmd != evt.tag)
		NSLog(@"#### expected %c%c%c%c, received %@", (inCmd >> 24) & 0xFF, (inCmd >> 16) & 0xFF, (inCmd >> 8) & 0xFF, inCmd & 0xFF, evt.command);

	return evt;
}


/*------------------------------------------------------------------------------
	Receive a kDResult event and extract the result value.
	Args:		--
	Return:	error code

	TO DO:	handle endpoint/wrong event errors
------------------------------------------------------------------------------*/

- (NewtonErr) receiveResult
{
	NCDockEvent * evt = [self receiveEvent: kDResult];
	if (evt.tag != kDResult)
		ThrowErr(exComm, kDockErrProtocolError);
	return evt.value;
}


#pragma mark Information
/*------------------------------------------------------------------------------
	Return the user font on the Newton device.
	We need this when rendering exported text.
	Args:		--
	Return:	font ref
------------------------------------------------------------------------------*/

- (Ref) getUserFont
{
	RefVar args(MakeArray(1));
	SetArraySlot(args, 0, SYMA(userFont));
	return [self callGlobalFunction: "GetUserConfig" with: args];
}


/*------------------------------------------------------------------------------
	Return the user folders on the Newton device.
	We need this when displaying soup entries.
	Args:		--
	Return:	folders frame ref
------------------------------------------------------------------------------*/

- (Ref) getUserFolders
{
	RefVar args(MakeArray(1));
	SetArraySlot(args, 0, SYMA(userFolders));
	return [self callGlobalFunction: "GetUserConfig" with: args];
}


/*------------------------------------------------------------------------------
	Return the gestalt of the Newton device.
	We need this when reporting device info.
	Args:		info			gestalt selector
	Return:	gestalt info
------------------------------------------------------------------------------*/

- (Ref) getGestalt: (uint32_t) info
{
	RefVar args(MakeArray(1));
	SetArraySlot(args, 0, MAKEINT(info));
	return [self callGlobalFunction: "Gestalt" with: args];
}


/*------------------------------------------------------------------------------
	Fetch the hexade of the Newton device.
	The hexade is set by AviD’s Fix2010.
	We need this when reporting the last sync time -- in this case Newton uses
	the unpatched Time() function.
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/

- (void) getHexade
{
	RefVar args(MakeArray(1));
	SetArraySlot(args, 0, MakeSymbol("hexade"));
	Ref r = [self callGlobalFunction: "GetUserConfig" with: args];
	tHexade = ISINT(r) ? (int)RVALUE(r) : 0;

	// while we’re here, also set up the difference between Newton and desktop time
	int now = kMinutes1904to1970 + time(NULL)/60;		// seconds since 1970 -> minutes since 1904
	tDelta = [self setLastSyncTime:0] - now;
}


/*------------------------------------------------------------------------------
	Set the date and time on the Newton device.
	Args:		time in seconds since 1/1/1904
	Return:	NILREF
------------------------------------------------------------------------------*/

- (Ref) setDateTime: (Ref) inTime
{
	RefVar args(MakeArray(1));
	SetArraySlot(args, 0, inTime);
	return [self callGlobalFunction: "SetTime" with: args];
}


/*------------------------------------------------------------------------------
	Set the status text on the Newton device.
	This is a Newton 2.1 function, probably never used.
	Args:		text to display
	Return:	--
------------------------------------------------------------------------------*/

- (void) setStatusText: (const UniChar *) inText
{
//	if (targetProtocol > kDanteProtocolVersion)
	{
		RefVar str(MakeString(inText));
		[self sendEvent: kDSetStatusText ref: str];
		[self receiveResult];
	//	if (result == kDUnknownCommand) throw();
	}
}


/*------------------------------------------------------------------------------
	Set the time to be stored as the last sync of the Newton device.
	Args:		inTime
	Return:	the previous sync time
------------------------------------------------------------------------------*/
#define kTimeOfTranquilityBegin 46811520	// StringToDate("Jan 1, 1993 12 AM")
#define kTimeOfTranquilityEnd   55226880	// StringToDate("Jan 1, 2009 12 AM")
#define kLengthOfHexade (kTimeOfTranquilityEnd - kTimeOfTranquilityBegin)

- (uint32_t) setLastSyncTime: (uint32_t) inTime
{
	[self sendEvent: kDLastSyncTime value: (int)inTime];
	NCDockEvent * evt = [self receiveEvent: kDCurrentTime];
	int currentTime = evt.value;
	// Newton may have been patched by AviD’s Fix2010
	// Newton returns unpatched value of Time() so patch it here
	return currentTime + tHexade * kLengthOfHexade;
}


#pragma mark Stores
/*------------------------------------------------------------------------------
	Return an array of frames describing the stores on the Newton device.
	Args:		--
	Return:	array ref
------------------------------------------------------------------------------*/

- (Ref) getAllStores
{
	NCDockEvent * evt = [self sendEvent: kDGetStoreNames expecting: kDStoreNames];
	return evt.ref;
}


/*------------------------------------------------------------------------------
	Return a frame describing the default store on the Newton device.
	Args:		--
	Return:	frame ref
------------------------------------------------------------------------------*/

- (Ref) getDefaultStore
{
	NCDockEvent * evt = [self sendEvent: kDGetDefaultStore expecting: kDDefaultStore];
	return evt.ref;
}


/*------------------------------------------------------------------------------
	Set the default store on the Newton device.
	Args:		inStore			nil => set default store
				info
	Return:	error code
------------------------------------------------------------------------------*/

- (NewtonErr) setCurrentStore: (RefArg) inStore
								 info: (BOOL) inSetStoreInfo
{
	if (ISNIL(inStore))
	{
		[self sendEvent: kDSetStoreToDefault];
	}
	else
	{
		RefVar store(AllocateFrame());
		SetFrameSlot(store, SYMA(name), GetFrameSlot(inStore, SYMA(name)));
		SetFrameSlot(store, SYMA(kind), GetFrameSlot(inStore, SYMA(kind)));
		SetFrameSlot(store, SYMA(signature), GetFrameSlot(inStore, SYMA(signature)));
		if (inSetStoreInfo)
			SetFrameSlot(store, SYMA(info), GetFrameSlot(inStore, SYMA(info)));

		[self sendEvent: kDSetCurrentStore ref: store];
	}
	return [self receiveResult];
}


#pragma mark Soup Info
/*------------------------------------------------------------------------------
	Return an array of arrays describing the soups on the current store
	on the Newton device.
	Each array entry is [name, signature].
	Args:		--
	Return:	array ref
------------------------------------------------------------------------------*/

- (Ref) getAllSoups
{
	RefVar soups;

	NCDockEvent * evt = [self sendEvent: kDGetSoupNames expecting: kDSoupNames];

	// Event parms:
	//		Ref			array of soup names
	//		Ref			array of soup signatures

	newton_try
	{
		RefVar names(evt.ref);
		RefVar signatures(evt.ref2);
		ASSERT(Length(names) == Length(signatures));

		soups = MakeArray(2);
		SetArraySlot(soups, 0, names);
		SetArraySlot(soups, 1, signatures);
	}
	newton_catch_all
	{
		soups = NILREF;
	}
	end_try;

	return soups;
}


/*------------------------------------------------------------------------------
	Get soup indexes.
	Args:		--
	Return:	soup indexes ref
------------------------------------------------------------------------------*/

- (Ref) getSoupIndexes
{
	NCDockEvent * evt = [self sendEvent: kDGetIndexDescription expecting: kDIndexDescription];
	return evt.ref;
}


/*------------------------------------------------------------------------------
	Get soup info.
	Args:		--
	Return:	soup info ref
------------------------------------------------------------------------------*/

- (Ref) getSoupInfo
{
	NCDockEvent * evt = [self sendEvent: kDGetSoupInfo expecting: kDSoupInfo];
	return evt.ref;
}


/*------------------------------------------------------------------------------
	Set soup info.
	Args:		inSoupInfo
	Return:	result code
------------------------------------------------------------------------------*/

- (NewtonErr)setSoupInfo:(RefArg)inSoupInfo
{
	[self sendEvent:kDSetSoupInfo ref:inSoupInfo];
	return [self receiveResult];
}


#pragma mark Soups
/*------------------------------------------------------------------------------
	Create a soup.
	Args:		inSoupName
				inSoupIndex			an array of index specs
	Return:	result code
------------------------------------------------------------------------------*/

- (NewtonErr) createSoup: (RefArg) inSoupName
				  index: (RefArg) inSoupIndex
{
	RefVar soupName(DeepClone(inSoupName));
	uint32_t soupNameLen = Length(soupName);
	unsigned int alignedSoupNameLen = ALIGN(soupNameLen,4);
	UniChar * s = (UniChar *) BinaryData(soupName);
#if defined(hasByteSwapping)
	UniChar * ss = s;
	for (ArrayIndex i = soupNameLen/sizeof(UniChar); i > 0; i--, ss++)
		*ss = BYTE_SWAP_SHORT(*ss);
#endif

	unsigned int soupIndexLen = (unsigned int)FlattenRefSize(inSoupIndex);
	unsigned int numOfBytes = sizeof(soupNameLen) + alignedSoupNameLen + soupIndexLen;
	CPtrPipe pipe;

	// Event parms:
	//		long		soup name length
	//		char[]	soup name					aligned x4
	//		Ref		soup indexes
	char * parms = (char *) malloc(numOfBytes);
	// append name length
	*(uint32_t *)parms = CANONICAL_LONG(soupNameLen);
	// append name, aligned on 4-bytes
	memcpy(parms + sizeof(soupNameLen), s, soupNameLen);
	int delta = alignedSoupNameLen - soupNameLen;
	if (delta > 0)
		memset(parms + sizeof(soupNameLen) + soupNameLen, 0, delta);
	// append indexes
	pipe.init(parms + sizeof(soupNameLen) + alignedSoupNameLen, soupIndexLen, NO, nil);
	FlattenRef(inSoupIndex, pipe);

	[self sendEvent: kDCreateSoup data: parms length: numOfBytes];
	free(parms);

	return [self receiveResult];	// kDResult?
}


/*------------------------------------------------------------------------------
	Set the soup for the following entry functions.
	Args:		inSoupName
	Return:	result code
------------------------------------------------------------------------------*/

- (NewtonErr) setCurrentSoup: (RefArg) inSoupName
{
	VALIDARG(IsString(inSoupName));

	unsigned int dataLen = MIN(Length(inSoupName), 64);
	UniChar soupName[64];					// spec says soup name limit is 25 unichars
	memcpy(soupName, BinaryData(inSoupName), dataLen);
#if defined(hasByteSwapping)
	UniChar * s = soupName;
	for (int i = dataLen/sizeof(UniChar); i > 0; i--, s++)
		*s = BYTE_SWAP_SHORT(*s);
#endif

	[self sendEvent: kDSetCurrentSoup data: soupName length: dataLen];
	return [self receiveResult];
}


/*------------------------------------------------------------------------------
	Empty the currently set soup.
	Args:		--
	Return:	result code
------------------------------------------------------------------------------*/

- (NewtonErr) emptySoup
{
	[self sendEvent: kDEmptySoup];
	return [self receiveResult];
}


/*------------------------------------------------------------------------------
	Delete the currently set soup.
	Args:		--
	Return:	result code
------------------------------------------------------------------------------*/

- (NewtonErr) deleteSoup
{
	[self sendEvent: kDDeleteSoup];
	return [self receiveResult];
}


/*------------------------------------------------------------------------------
	Add an entry to the currently set soup.
	The frame’s _uniqueId and _modTime are updated in line with the soup entry.
	Args:		ioEntry			a frame
	Return:	--
------------------------------------------------------------------------------*/

- (int) currentTime
{
	int now = kMinutes1904to1970 + time(NULL)/60;		// seconds since 1970 -> minutes since 1904
	return now + tDelta;
}


- (NewtonErr) addEntry: (RefArg) ioEntry
{
	[self sendEvent: kDAddEntry ref: ioEntry];
	NCDockEvent * evt = [self receiveEvent: kDAddedID];
	int entryId = evt.value;
	int entryModTime = [self currentTime];

	SetFrameSlot(ioEntry, SYMA(_uniqueId), MAKEINT(entryId));
	SetFrameSlot(ioEntry, SYMA(_modTime), MAKEINT(entryModTime));

	return noErr;
}


/*------------------------------------------------------------------------------
	Change an entry in the currently set soup.
	Args:		inEntry			a frame
	Return:	result code
------------------------------------------------------------------------------*/

- (NewtonErr) changeEntry: (RefArg) inEntry
{
	[self sendEvent: kDChangedEntry ref: inEntry];
	return [self receiveResult];
}


/*------------------------------------------------------------------------------
	Delete an entry from the currently set soup.
	Args:		inEntry			the soup entry
	Return:	error code
------------------------------------------------------------------------------*/

- (NewtonErr) deleteEntry: (RefArg) inEntry
{
	VALIDARG(IsFrame(inEntry));

	RefVar id(GetFrameSlot(inEntry, SYMA(_uniqueId)));
	VALIDARG(IsInt(id));

	RefVar idList(MakeArray(1));
	SetArraySlot(idList, 0, id);
	return [self deleteEntries: idList];
}


/*------------------------------------------------------------------------------
	Delete entries from the currently set soup.
	Args:		inEntryList		array of soup entries
	Return:	error code
------------------------------------------------------------------------------*/

- (NewtonErr) deleteEntryList: (RefArg) inEntryList
{
	VALIDARG(IsArray(inEntryList));

	int i, count = Length(inEntryList);
	RefVar idList(MakeArray(count));
	for (i = 0; i < count; i++)
	{
		RefVar entry(GetArraySlot(inEntryList, i));
		VALIDARG(IsFrame(entry));
		RefVar id(GetFrameSlot(entry, SYMA(_uniqueId)));
		VALIDARG(IsInt(id));
		SetArraySlot(idList, i, id);
	}
	return [self deleteEntries: idList];
}


/*------------------------------------------------------------------------------
	Delete an entry from the currently set soup.
	Args:		inEntryID		the entry id
	Return:	error code
------------------------------------------------------------------------------*/

- (NewtonErr) deleteEntryId: (RefArg) inEntryId
{
	VALIDARG(IsInt(inEntryId));

	RefVar idList(MakeArray(1));
	SetArraySlot(idList, 0, inEntryId);
	return [self deleteEntries: idList];
}


/*------------------------------------------------------------------------------
	Delete entries from the currently set soup.
	Args:		inEntryIdList	array of entry ids
	Return:	error code
------------------------------------------------------------------------------*/

- (NewtonErr) deleteEntryIdList: (RefArg) inEntryIdList
{
	VALIDARG(IsArray(inEntryIdList));

	int i, count = Length(inEntryIdList);
	for (i = 0; i < count; i++)
		VALIDARG(IsInt(GetArraySlot(inEntryIdList, i)));

	return [self deleteEntries: inEntryIdList];
}


/*------------------------------------------------------------------------------
	Helper: delete entries from the currently set soup.
	Args:		inEntries		array of entry ids
	Return:	result code
------------------------------------------------------------------------------*/

- (NewtonErr) deleteEntries: (RefArg) inEntries
{
	NewtonErr err = noErr;
	unsigned int numOfEntries = Length(inEntries);

	if (numOfEntries > 0)
	{
		unsigned int numOfBytes = (1+numOfEntries)*sizeof(int32_t);

		// Event parms:
		//		long		number of entries
		//		long[]	ids of entries to delete
		char * parms = (char *) malloc(numOfBytes);
		int32_t * p = (int32_t *)parms;
		*p++ = CANONICAL_LONG(numOfEntries);
		for (int i = 0; i < numOfEntries; i++)
		{
			int32_t num = (int32_t)RVALUE(GetArraySlot(inEntries, i));
			*p++ = CANONICAL_LONG(num);
		}
		[self sendEvent: kDDeleteEntries data: parms length: numOfBytes];
		free(parms);

		err = [self receiveResult];
	}
	return err;
}


/*------------------------------------------------------------------------------
	Return an entry in the currently set soup.
	Args:		inUniqueId		the entry id
	Return:	entry ref
------------------------------------------------------------------------------*/

- (Ref) getEntry: (int) inUniqueId
{
	VALIDARG(inUniqueId != 0);

	[self sendEvent: kDReturnEntry value: inUniqueId];
	NCDockEvent * evt = [self receiveEvent: kDEntry];
	return evt.ref;
}


/*------------------------------------------------------------------------------
	Return the ids of all entries in the currently set soup.
	Args:		--
	Return:	array ref
------------------------------------------------------------------------------*/

- (Ref) getEntryIds
{
	RefVar entryIds;

	newton_try
	{
		NCDockEvent * evt = [self sendEvent: kDGetSoupIDs expecting: kDSoupIDs];

		// data are longs; number of ids followed by the ids
		int32_t * idArray = (int32_t *)evt.data;
		ArrayIndex	numOfIds = CANONICAL_LONG(*idArray);
		idArray++;

		entryIds = MakeArray(numOfIds);
		for (ArrayIndex i = 0; i < numOfIds; i++, idArray++)
		{
			SetArraySlot(entryIds, i, CANONICAL_LONG(*idArray));
		}
	}
	newton_catch_all
	{
		entryIds = NILREF;
	}
	end_try;

	return entryIds;
}


#pragma mark Cursor
/*------------------------------------------------------------------------------
	Query a soup.
	Args:		inSoupName
				inQuerySpec
	Return:	a soup cursor
------------------------------------------------------------------------------*/

- (NCCursor *) query: (RefArg) inSoupName
					 spec: (RefArg) inQuerySpec
{
	VALIDARG(ISNIL(inSoupName) || IsString(inSoupName));
	VALIDARG(ISNIL(inQuerySpec) || IsFrame(inQuerySpec));

	NCCursor *	cursor = [[NCCursor alloc] init: (id)self];
	[cursor query: inSoupName spec: inQuerySpec];
	return cursor;
}


#pragma mark Packages
/*------------------------------------------------------------------------------
	Send a package.
	Args:		inURL			URL of package file
				inCallback	block to call back to indicate progress
				inFrequency	number of bytes to send between callbacks
								* we assume this is a sensible size *
	Return:	--
------------------------------------------------------------------------------*/

- (void) sendPackage: (NSURL *) inURL callback: (NCProgressCallback) inCallback frequency: (unsigned int) inFrequency
{
	[self resetTickler: kDefaultTimeout];
	[dockEventQueue sendEvent: kDLoadPackage file: inURL callback: inCallback frequency: inFrequency];
}


#pragma mark Protocol Extensions
/*------------------------------------------------------------------------------
	Load a protocol extension.
	Args:		inId
				inFunc
	Return:	load result
------------------------------------------------------------------------------*/

- (NewtonErr) loadExtension: (NSString *) inExtensionName
{
	NSURL * url = [[NSBundle mainBundle] URLForResource: inExtensionName withExtension: @"stream"];
	// files MUST be prefixed w/ (int32_t) extensionId
	[self resetTickler: kDefaultTimeout];
	[dockEventQueue sendEvent: kDRegProtocolExtension file: url callback: nil frequency: 0];
	return [self receiveResult];
}


/*------------------------------------------------------------------------------
	Call a protocol extension.
	Args:		inId
				inArgs
	Return:	the event sent in reply
------------------------------------------------------------------------------*/

- (NCDockEvent *) callExtension: (EventType) inId with: (RefArg) inArgs
{
	ASSERT(inId != 0);

	if (ISNIL(inArgs))
		[self sendEvent: inId];
	else
		[self sendEvent: inId ref: inArgs];

	NCDockEvent * evt = [self receiveEvent: kDAnyEvent];	// any event is acceptable
																			// although kDResult would be usual
	return evt;
}


/*------------------------------------------------------------------------------
	Remove an installed protocol extension.
	Args:		inId
	Return:	removal result
------------------------------------------------------------------------------*/

- (NewtonErr) removeExtension: (EventType) inId
{
	ASSERT(inId != 0);

	[self sendEvent: kDRemoveProtocolExtension value: inId];
	return [self receiveResult];
}


#pragma mark Global/Root Functions
/*------------------------------------------------------------------------------
	Call a global function.
	Args:		inFunctionName
				inArgs
	Return:	function result
------------------------------------------------------------------------------*/

- (Ref) callGlobalFunction: (const char *) inFunctionName with: (RefArg) inArgs
{
	ASSERT(inFunctionName != NULL);
	VALIDARG(IsArray(inArgs));

	return [self callGlobalFunctionOrRootMethod: kDCallGlobalFunction name: inFunctionName with: inArgs];
}


/*------------------------------------------------------------------------------
	Call a root method.
	Args:		inMethodName
				inArgs
	Return:	method result
------------------------------------------------------------------------------*/

- (Ref) callRootMethod: (const char *) inMethodName with: (RefArg) inArgs
{
	ASSERT(inMethodName != NULL);
	VALIDARG(IsArray(inArgs));

	return [self callGlobalFunctionOrRootMethod: kDCallRootMethod name: inMethodName with: inArgs];
}


/*------------------------------------------------------------------------------
	Helper: global/root call.
	Args:		inCmd
				inName
				inArgs
	Return:	function result
------------------------------------------------------------------------------*/

- (Ref) callGlobalFunctionOrRootMethod: (EventType) inCmd name: (const char *) inName with: (RefArg) inArgs
{
	ASSERT(inName != NULL);

	RefVar name(MakeSymbol(inName));
	unsigned int nameSize = (unsigned int)FlattenRefSize(name);
	unsigned int argsSize = (unsigned int)FlattenRefSize(inArgs);
	unsigned int numOfBytes = nameSize + argsSize;
	CPtrPipe pipe;

	// Event parms:
	//		Ref	function/method name
	//		Ref	function/method args
	char * parms = (char *) malloc(numOfBytes);
//memset(parms, 0, numOfBytes);
	pipe.init(parms, nameSize, NO, nil);
	FlattenRef(name, pipe);
	pipe.init(parms + nameSize, argsSize, NO, nil);
	FlattenRef(inArgs, pipe);
	[self sendEvent: inCmd length: kIndeterminateLength data: parms length: numOfBytes];
	free(parms);

	NCDockEvent * evt = [self receiveEvent: kDAnyEvent];
	if (evt.tag == kDCallResult)
		return evt.ref;
	return NILREF;
}


@end
