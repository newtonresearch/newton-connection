/*
	File:		DockEvent.mm

	Contains:	Newton Dock event implementation.

	Written by:	Newton Research Group, 2011.
*/

#import "DockEventQueue.h"
#import "DockErrors.h"
#import "Logging.h"


/* -----------------------------------------------------------------------------
	N C D o c k E v e n t
----------------------------------------------------------------------------- */

@interface NCDockEvent ()
{
	DockEventHeader header;
	unsigned char	buf[kEventBufSize+4];
	unsigned int	bufLength;
	unsigned int	alignedLength;
	void *			_data;
	unsigned int	_dataLength;
}
@end


@implementation NCDockEvent
#pragma mark - Event builders
/*------------------------------------------------------------------------------
	Make an event.
	Args:		inCmd
	Return:	an event object
------------------------------------------------------------------------------*/

+ (NCDockEvent *)makeEvent:(EventType)inCmd {
	NCDockEvent * evt = [[NCDockEvent alloc] initEvent:inCmd];
	return evt;
}


/*------------------------------------------------------------------------------
	Make an event.
	Args:		inCmd
				inValue
	Return:	an event object
------------------------------------------------------------------------------*/

+ (NCDockEvent *)makeEvent:(EventType)inCmd value:(int)inValue {
	NCDockEvent * evt = [[NCDockEvent alloc] initEvent:inCmd];
	evt.dataLength = sizeof(ULong);
	evt.value = inValue;
	return evt;
}


/*------------------------------------------------------------------------------
	Make an event.
	Args:		inCmd
				inRef
	Return:	an event object
------------------------------------------------------------------------------*/

+ (NCDockEvent *)makeEvent:(EventType)inCmd ref:(RefArg)inRef {
	NCDockEvent * evt = [[NCDockEvent alloc] initEvent:inCmd];
	evt.dataLength = (unsigned int)FlattenRefSize(inRef);
	evt.ref = inRef;
	return evt;
}


/*------------------------------------------------------------------------------
	Make an event.
	Args:		inCmd
				inURL
	Return:	an event object
------------------------------------------------------------------------------*/

+ (NCDockEvent *)makeEvent:(EventType)inCmd file:(NSURL *)inURL {
	NCDockEvent * evt = [[NCDockEvent alloc] initEvent:inCmd];
	// open the file and determine its length
	FILE * fref = fopen(inURL.fileSystemRepresentation, "r");
	fseek(fref, 0, SEEK_END);
	evt.dataLength = (unsigned int)ftell(fref);
	fclose(fref);
	evt.file = inURL;
	return evt;
}


/*------------------------------------------------------------------------------
	Make an event.
	The crazy, crazy dock protocol uses a length word in all events - but this
	does not necessarily indicate the length of data. It might mean a number of
	Unicode characters, for example.
	Args:		inCmd				event code
				inLength			length to use for event
				inData			event data
				inDataLength	actual length of event data
	Return:	an event object
------------------------------------------------------------------------------*/

+ (NCDockEvent *)makeEvent:(EventType)inCmd length:(unsigned int)inLength data:(const void *)inData length:(unsigned int)inDataLength {
	NCDockEvent * evt = [[NCDockEvent alloc] initEvent:inCmd];
	evt.dataLength = inDataLength;	// MUST set this first...
	evt.length = inLength;				// overwriting protocol’s idea of data length
	evt.data = (void *)inData;			// ...so data can be alloc’d at the right size

	return evt;
}


#pragma mark - Initialisation
/*------------------------------------------------------------------------------
	Initialize null event instance.
	Args:		--
	Return:	self
------------------------------------------------------------------------------*/

- (id)init {
	if (self = [super init]) {
		header.evtClass = kNewtEventClass;
		header.evtId = kDockEventId;
		header.tag = 0;
		header.length = 0;
		alignedLength = 0;
		*(int32_t *)buf = 0;
		_data = NULL;
		_dataLength = 0;
		file = NULL;
	}
	return self;
}


/*------------------------------------------------------------------------------
	Initialize event instance.
	Args:		--
	Return:	self
------------------------------------------------------------------------------*/

- (id)initEvent:(EventType)inCmd {
	if (self = [self init]) {
		header.tag = inCmd;
	}
	return self;
}


/*------------------------------------------------------------------------------
	Dispose event instance.
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/

- (void)dealloc {
	if (_data)
		free(_data), _data = NULL;
}


/* -----------------------------------------------------------------------------
	Description for debug output.
	In the format:
		newt-dock-dres [4] -10221
----------------------------------------------------------------------------- */
void
FillIn(char * ioBuf, uint32_t inCmd, char inSeparator) {
	ioBuf[0] = inCmd >> 24;
	ioBuf[1] = inCmd >> 16;
	ioBuf[2] = inCmd >>  8;
	ioBuf[3] = inCmd;
	ioBuf[4] = inSeparator;
}

- (NSString *)description {
	int len;
	char dbuf[1024];
	FillIn(dbuf+ 0, header.evtClass, '-');
	FillIn(dbuf+ 5, header.evtId, '-');
	FillIn(dbuf+10, header.tag, ' ');
	len = sprintf(dbuf+15, "[%u]", _dataLength) + 15;
	if (_dataLength > 0) {
		if (_dataLength == 4) {
			len += sprintf(dbuf+len, " %d", self.value);
		}
		char * s = dbuf+len;
		unsigned char * p = (unsigned char *)self.data;
		for (int i = MIN(_dataLength,256); i > 0; i--, p++, s+=3) {
			sprintf(s, " %02X", *p);
		}
		if (_dataLength > 256) {
			sprintf(s, "..."), s+=3;
		}
		*s = 0;
	}
	return [NSString stringWithUTF8String:dbuf];
}


#pragma mark - Property accessors
/* -----------------------------------------------------------------------------
	The four-char event tag.
----------------------------------------------------------------------------- */
-(EventType)tag {
	return header.tag;
}


/* -----------------------------------------------------------------------------
	The four-char event tag expressed as a string.
----------------------------------------------------------------------------- */
- (NSString *)command {
	return [NSString stringWithFormat: @"%c%c%c%c", (header.tag >> 24) & 0xFF, (header.tag >> 16) & 0xFF, (header.tag >> 8) & 0xFF, header.tag & 0xFF];
}


/* -----------------------------------------------------------------------------
	The overall length of the event structure.
----------------------------------------------------------------------------- */
- (void)setLength:(unsigned int)inLength {
	header.length = inLength;
}

- (unsigned int)length {
	return header.length;
}


/* -----------------------------------------------------------------------------
	The length of data contained in the event.
----------------------------------------------------------------------------- */
- (void)setDataLength:(unsigned int)inLength {
	_dataLength = inLength;
	header.length = inLength;
	alignedLength = LONGALIGN(inLength);
}

- (unsigned int)dataLength {
	return _dataLength;
}


/* -----------------------------------------------------------------------------
	The data contained in the event.
----------------------------------------------------------------------------- */
- (void)setData:(void *)inData {
	if (_data) {
		free(_data), _data = NULL;
	}
	// length MUST have been set previously
	if (_dataLength > kEventBufSize) {
		_data = malloc(alignedLength);
	}
	memcpy(self.data, inData, _dataLength);

	// pad with zeroes
	int delta = alignedLength - _dataLength;
	if (delta > 0) {
		memset((char *)self.data + _dataLength, 0, delta);
	}
}

- (void *)data {
	return _data ? _data : buf;
}


/* -----------------------------------------------------------------------------
	int32_t-sized data contained in the event.
----------------------------------------------------------------------------- */
- (void)setValue:(int)inValue {
	if (_data) {
		free(_data), _data = NULL;
	}
	self.dataLength = sizeof(int32_t);
	*(int32_t *)buf = CANONICAL_LONG(inValue);
}

- (int)value {
	int32_t * p = (int32_t *)self.data;
	return CANONICAL_LONG(*p);
}


/* -----------------------------------------------------------------------------
	NSOF-encoded Ref data contained in the event.
----------------------------------------------------------------------------- */
- (void)setRef:(Ref)inRef {
	if (_data) {
		free(_data), _data = NULL;
	}
	self.dataLength = (unsigned int)FlattenRefSize(inRef);
	if (header.length > kEventBufSize) {
		_data = malloc(alignedLength);
	}
	CPtrPipe pipe;
	pipe.init(self.data, header.length, NO, NULL);
	FlattenRef(inRef, pipe);

	// pad with zeroes
	int delta = alignedLength - header.length;
	if (delta > 0) {
		memset((char *)self.data + header.length, 0, delta);
	}
}

- (Ref)ref {
	CPtrPipe pipe;
	pipe.init(self.data, header.length, NO, NULL);
	return UnflattenRef(pipe);
}


/* -----------------------------------------------------------------------------
	First NSOF-encoded Ref data contained in an event.
----------------------------------------------------------------------------- */
- (Ref)ref1 {
	CPtrPipe pipe;
	pipe.init(((char*)self.data)+4, header.length-4, NO, NULL);	// skip value
	return UnflattenRef(pipe);
}


/* -----------------------------------------------------------------------------
	Second NSOF-encoded Ref data contained in an event.
----------------------------------------------------------------------------- */
- (Ref)ref2 {
	CPtrPipe pipe;
	pipe.init(self.data, header.length, NO, NULL);
	UnflattenRef(pipe);	// discard first ref
	return UnflattenRef(pipe);
}


/* -----------------------------------------------------------------------------
	The URL of the file containing data for the event.
----------------------------------------------------------------------------- */
@synthesize file;


#pragma mark - Receive event
/*------------------------------------------------------------------------------
	Add data from buffer to build event *including* data.
	Args:		inData
	Return:					noErr => we have built a full dock event
				kCommsPartialData => not enough data yet
------------------------------------------------------------------------------*/

- (NCError)build:(CChunkBuffer *)inData {
	static const unsigned char kDockHeader[8] = { 'n','e','w','t', 'd','o','c','k' };
	static int evtState = 0;
	static unsigned int reqLen;
	static unsigned char * dp;
	unsigned int actLen;
	NCError status = kCommsPartialData;

	XTRY
	{
		int ch;
		for (ch = 0; ch >= 0; ) {
			switch (evtState) {
			case 0:
				header.tag = 0;
				_dataLength = 0;
				if (_data != NULL && _data != buf)
					free(_data);
				_data = NULL;
			case 1:
			case 2:
			case 3:
			case 4:
			case 5:
			case 6:
			case 7:
//	scan for newt dock start-of-event
				XFAIL((ch = inData->nextChar()) < 0)
				if (ch == kDockHeader[evtState])
					evtState++;
				else
					evtState = 0;
				break;

			case 8:
			case 9:
			case 10:
			case 11:
//	read 4-char tag
				XFAIL((ch = inData->nextChar()) < 0)
				header.tag = (header.tag << 8) + ch;
				evtState++;
				break;

			case 12:
			case 13:
			case 14:
			case 15:
//	read 4-char length
				XFAIL((ch = inData->nextChar()) < 0)
				header.length = (header.length << 8) + ch;
				evtState++;
				break;

			case 16:
//	set up data/buffer
				if (_data)
					free(_data), _data = NULL;
				if (header.length == kIndeterminateLength) {
					// start reading it into buf (when that overflows data will be malloc’d)
					_dataLength = alignedLength = 0;
					bufLength = kEventBufSize;
					evtState = 20;
					break;
				}
				self.dataLength = header.length;
				reqLen = alignedLength;
				if (header.length > kEventBufSize)
					_data = malloc(reqLen);
				dp = (unsigned char *)self.data;
				evtState++;

			case 17:
//	read data
				if (reqLen != 0) {
					actLen = inData->read(dp, reqLen);
/*
if (reqLen > 4)
{
NSLog(@"-build: required %d, received %d", reqLen, actLen);
unsigned char * p = dp;
for (int y = 0; y < actLen; y += 32)
{
	char str[32*3 + 1];
	int x;
	for (x = 0; x < 32 && y + x < actLen; x++)
		sprintf(str+x*3, "%02X ", p[y+x]);
	if (x > 0)
	{
		str[x*3] = 0;
		NSLog(@"%s", str);
	}
}
}
*/
					dp += actLen;
					reqLen -= actLen;
					XFAILIF(reqLen != 0, ch = -1;)	// break out of the loop because we don’t have enough data yet
																// but return to this state next time data is received
				}
				// at this point data has been read, including any long-align padding
				// reset FSM for next time
				evtState = 0;
				status = noErr;	// noErr -- packet fully unframed
				ch = -1;				// fake value so we break out of the loop

MINIMUM_LOG {
	REPprintf("\n     <-- %c%c%c%c ", (header.tag >> 24) & 0xFF, (header.tag >> 16) & 0xFF, (header.tag >> 8) & 0xFF, header.tag & 0xFF);
	if (header.length == sizeof(int32_t)) { int v = self.value; REPprintf("%d (0x%08X) ", v, v); }
	else if (header.length > 0) REPprintf("[%d] ", header.length);
}
				break;

			case 20:
			case 21:
			case 22:
			case 23:
			case 24:
			case 25:
			case 26:
			case 27:
// keep buffering data until we encounter newtdock header in the stream
				XFAIL((ch = inData->nextChar()) < 0)
				if (ch == kDockHeader[evtState-20]) {
					// stream matches header
					evtState++;
					break;
				}
				if (evtState > 20) {
					const unsigned char * p = kDockHeader;
					while (evtState-- > 20) {
						[self addIndeterminateData: *p++];
					}
				}
				[self addIndeterminateData: ch];
				break;

			case 28:
				// at this point a header has been read
				// set up the actual length of data read
				self.dataLength = _dataLength;	// sic
				// reset FSM for next time, starting at the tag
				evtState = 8;
				status = noErr;	// noErr -- packet fully unframed
				ch = -1;				// fake value so we break out of the loop

MINIMUM_LOG {
	REPprintf("\n     <-- %c%c%c%c [%d] indeterminate", (header.tag >> 24) & 0xFF, (header.tag >> 16) & 0xFF, (header.tag >> 8) & 0xFF, header.tag & 0xFF, header.length);
}
			}
		}
	}
	XENDTRY;

	return status;
}


/*------------------------------------------------------------------------------
	Add a byte of indeterminately-sized data.
	Args:		inData
	Return:	--
------------------------------------------------------------------------------*/

- (void)addIndeterminateData:(unsigned char)inData {
	if (_dataLength == bufLength) {
		// we will overrun our buffer: alloc a larger one
		bufLength += 256;
		if (_data == NULL) {
			_data = malloc(bufLength);
			memcpy(_data, buf, _dataLength);
		} else {
			_data = realloc(_data, bufLength);
		}
	}
	*((unsigned char *)self.data + _dataLength++) = inData;
}


#pragma mark - Send event
/*------------------------------------------------------------------------------
	Send an event -- first 4 longs of self followed by the data.
	Need to pad the data sent to align on long.
		'newt'
		'dock'
		tag
		length
		data
	Args:		ep			endpoint
	Return:	--
------------------------------------------------------------------------------*/

- (NewtonErr)send:(NCEndpoint *)ep {
	return [self send: ep callback: 0 frequency: 0];
}


- (NewtonErr)send:(NCEndpoint *)ep callback:(NCProgressCallback)inCallback frequency:(unsigned int)inChunkSize {
	NewtonErr err = noErr;

MINIMUM_LOG {
	//NSLog(@"%@",[self description]);
	REPprintf("\n%c%c%c%c --> ", (header.tag >> 24) & 0xFF, (header.tag >> 16) & 0xFF, (header.tag >> 8) & 0xFF, header.tag & 0xFF);
	if (header.length == sizeof(int32_t)) { int v = self.value; REPprintf("%d (0x%08X) ", v, v); }
	else if (header.length > 0) REPprintf("[%d] ", header.length);
}

	if (inChunkSize == 0) {
		// send all in one go
		if (file)
			inChunkSize = _dataLength;
		else
			inChunkSize = alignedLength;
	} else {
		// sanity check
		if (inChunkSize < 256)
			inChunkSize = 256;
		if (inChunkSize > 4096)
			inChunkSize = 4096;
	}

#if defined(hasByteSwapping)
	header.evtClass = BYTE_SWAP_LONG(header.evtClass);
	header.evtId = BYTE_SWAP_LONG(header.evtId);
	header.tag = BYTE_SWAP_LONG(header.tag);
	header.length = BYTE_SWAP_LONG(header.length);
#endif

	XTRY
	{
		XFAILNOT(ep, err = kNCErrorWritingToPipe;)

// ideally what we should do is write to buffer until it’s full or we -flush

		if (file) {
			FILE * fref = fopen(file.fileSystemRepresentation, "r");
			XFAILIF(fref == NULL, err = kNCInvalidFile; )
			XTRY
			{
				int padding = alignedLength - _dataLength;
				off_t offset;
				void * chunk = malloc(sizeof(DockEventHeader) + LONGALIGN(inChunkSize));
				XFAILIF(chunk == NULL, err = kNCOutOfMemory; )

				memcpy(chunk, &header, sizeof(DockEventHeader));
				offset = sizeof(DockEventHeader);

				int amountRead, amountRemaining, amountDone = 0;
				if (inCallback)
					dispatch_async(dispatch_get_main_queue(), ^{ inCallback(_dataLength, amountDone); });

				fseek(fref, 0, SEEK_SET);
				for (amountRemaining = _dataLength; amountRemaining > 0; amountRemaining -= amountRead) {
					amountRead = inChunkSize;
					if (amountRead > amountRemaining)
						amountRead = amountRemaining;
					amountRead = (int)fread((char *)chunk+offset, 1, amountRead, fref);
					if (padding > 0 && amountRemaining - (amountDone + amountRead) == 0) {
						memset((char *)chunk+offset+amountRead, 0, padding);
						amountRead += padding;
					}
					if (inCallback) {
						// we want progress reporting so write synchronously
						XFAIL(err = [ep writeSync:chunk length:(unsigned int)offset + amountRead])
						if (NCDockEventQueue.sharedQueue.isEventReady)	// Newton is trying to tell us something
							break;
					} else {
						XFAIL(err = [ep write:chunk length:(unsigned int)offset + amountRead])
					}
					offset = 0;
					amountDone += amountRead;
					if (inCallback) {
						dispatch_async(dispatch_get_main_queue(), ^{ inCallback(_dataLength, amountDone); });
					}
				}
				free(chunk);
			}
			XENDTRY;
			fclose(fref);

		} else if (_data) {
			XFAIL(err = [ep write: &header length: sizeof(DockEventHeader)])

			char * chunk = (char *)self.data;

			int amountRead, amountRemaining, amountDone = 0;
			if (inCallback)
				dispatch_async(dispatch_get_main_queue(), ^{ inCallback(alignedLength, amountDone); });

			for (amountRemaining = alignedLength; amountRemaining > 0; amountRemaining -= amountRead) {
				amountRead = inChunkSize;
				if (amountRead > amountRemaining)
					amountRead = amountRemaining;
				if (inCallback) {
					// we want progress reporting so write synchronously
					XFAIL(err = [ep writeSync: chunk length: amountRead])
					if (NCDockEventQueue.sharedQueue.isEventReady)	// Newton is trying to tell us something
						break;
				} else {
					XFAIL(err = [ep write: chunk length: amountRead])
				}
				chunk += amountRead;
				amountDone += amountRead;
				if (inCallback) {
					dispatch_async(dispatch_get_main_queue(), ^{ inCallback(alignedLength, amountDone); });
				}
			}

		} else if (header.length == kIndeterminateLength) {
			// WTF was that Dock Protocol engineer thinking?
			// if we send refs we MUST NOT send the actual length with the command
			// and we MUST NOT align the data
			XFAIL(err = [ep write: &header length: sizeof(DockEventHeader) + _dataLength])

		} else {
			XFAIL(err = [ep write: &header length: sizeof(DockEventHeader) + alignedLength])
		}
	}
	XENDTRY;

#if defined(hasByteSwapping)
	header.evtClass = BYTE_SWAP_LONG(header.evtClass);
	header.evtId = BYTE_SWAP_LONG(header.evtId);
	header.tag = BYTE_SWAP_LONG(header.tag);
	header.length = BYTE_SWAP_LONG(header.length);
#endif

	return err;
}

@end

