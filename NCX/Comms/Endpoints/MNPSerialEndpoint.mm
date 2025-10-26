/*
	File:		MNPSerialEndpoint.mm

	Contains:	Implementation of the MNP serial endpoint.

	Written by:	Newton Research Group, 2005.
*/

#define forAdriano 0

#import "MNPSerialEndpoint.h"
#import "SerialPrefsViewController.h"
#import "DockErrors.h"
#import "Logging.h"

#define ERRBASE_SERIAL					(-18000)	// Newton SerialTool errors
#define kSerErrCRCError					(ERRBASE_SERIAL -  4)	// CRC error on input framing


/* -----------------------------------------------------------------------------
	C o n s t a n t s
----------------------------------------------------------------------------- */

enum
{
	kLRPacketType = 1,	// negotiate
	kLDPacketType,			// disconnect
	kLxPacketType,
	kLTPacketType,			// data
	kLAPacketType,			// acknowledge
	kLNPacketType,
	kLNAPacketType
};


const unsigned char kLRPacket[] =
{
	23,		/* Length of header */
	kLRPacketType,
	/* Constant parameter 1 */
	0x02,
	0x01, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0xFF,
	/* Constant parameter 2 */
	0x02, 0x01, 0x02,			/* Octet-oriented framing mode */
	0x03, 0x01, 0x01,			/* k = 1 */
	0x04, 0x02, 0x40, 0x00,	/* N401 = 64 */
	0x08, 0x01, 0x03			/* N401 = 256 & fixed LT, LA frames */
};
/*
	26
	01 02
	01 06 01 00 00 00 00 FF
	02 01 02
	03 01 08
	04 02 40 00
	08 01 03
	09 01 01
	0E 04 03 04 00 FA
	C5 06 01 04 00 00 E1 00 
*/
const unsigned char kLDPacket[] =
{
	4,			/* Length of header */
	kLDPacketType,
	0x01, 0x01, 0xFF
};

const unsigned char kLTPacket[] =
{
	2,			/* Length of header */
	kLTPacketType,
	0			/* Sequence number */
};

const unsigned char kLAPacket[] =
{
	3,			/* Length of header */
	kLAPacketType,
	0,			/* Sequence number */
	1			/* N(k) = 1 */
};


/* -----------------------------------------------------------------------------
	D a t a
----------------------------------------------------------------------------- */

extern BOOL gTraceIO;

int doHandshaking = 0;

static unsigned char ltPacketHeader[sizeof(kLTPacket)];
static unsigned char laPacketHeader[sizeof(kLAPacket)];


/* -----------------------------------------------------------------------------
	S e r i a l   P o r t
--------------------------------------------------------------------------------
	Return an iterator across all known serial ports.

	Each serial device object has a property with key kIOSerialBSDTypeKey
	and a value that is one of
		kIOSerialBSDAllTypes,
		kIOSerialBSDModemType,
		kIOSerialBSDRS232Type.
	You can experiment with the matching by changing the last parameter
	in the call to CFDictionarySetValue.

	Caller is responsible for releasing the iterator when iteration is complete.
----------------------------------------------------------------------------- */
extern "C" int REPprintf(const char * inFormat, ...);

kern_return_t
FindSerialPorts(io_iterator_t * matchingServices) {
	kern_return_t result;
	CFMutableDictionaryRef classesToMatch;

	// Serial devices are instances of class IOSerialBSDClient
	classesToMatch = IOServiceMatching(kIOSerialBSDServiceValue);
	if (classesToMatch) {
		CFDictionarySetValue(classesToMatch,
									CFSTR(kIOSerialBSDTypeKey),
									CFSTR(kIOSerialBSDAllTypes));		// was kIOSerialBSDRS232Type
	} else {
		REPprintf("IOServiceMatching returned a NULL dictionary.");
	}

	result = IOServiceGetMatchingServices(kIOMasterPortDefault, classesToMatch, matchingServices);    
	if (result != KERN_SUCCESS) {
		REPprintf("IOServiceGetMatchingServices returned %d.", result);
	}
	return result;
}


/* -----------------------------------------------------------------------------
	M N P S e r i a l E n d p o i n t
----------------------------------------------------------------------------- */

@implementation MNPSerialEndpoint

/* -----------------------------------------------------------------------------
	Check availability of MNP Serial endpoint.
	Available if IOKit can find any serial ports.
----------------------------------------------------------------------------- */

+ (BOOL)isAvailable {
	BOOL result = NO;
	io_iterator_t serialPortIterator;
	io_object_t service;

	if (FindSerialPorts(&serialPortIterator) == KERN_SUCCESS) {
		if ((service = IOIteratorNext(serialPortIterator)) != 0) {
			result = YES;
			IOObjectRelease(service);
		}
		IOObjectRelease(serialPortIterator);
	}
	return result;
}


/* -----------------------------------------------------------------------------
	Return array of strings for names and corresponding /dev paths
	of all known serial ports.
----------------------------------------------------------------------------- */

+ (NCError)getSerialPorts:(NSArray *__strong *)outPorts {

	NCError			result = -1;
	io_iterator_t	serialPortIterator;
	io_object_t		service;

	NSString * portName, * portPath;
	NSMutableArray * ports = [NSMutableArray arrayWithCapacity: 8];
	NSAssert(outPorts != nil, @"nil pointer to serial ports array");
	*outPorts = nil;

	if (FindSerialPorts(&serialPortIterator) == KERN_SUCCESS) {
		while ((service = IOIteratorNext(serialPortIterator)) != 0) {
			portName = (NSString *) CFBridgingRelease(IORegistryEntryCreateCFProperty(service,
																											  CFSTR(kIOTTYDeviceKey),
																											  kCFAllocatorDefault,
																											  0));
			// Get the callout device's path (/dev/cu.xxxxx). The callout device should almost always be
			// used: the dialin device (/dev/tty.xxxxx) would be used when monitoring a serial port for
			// incoming calls, e.g. a fax listener.
			portPath = (NSString *) CFBridgingRelease(IORegistryEntryCreateCFProperty(service,
																											  CFSTR(kIOCalloutDeviceKey),
																											  kCFAllocatorDefault,
																											  0));
			[ports addObject:@{ @"name":portName, @"path":portPath }];
			portPath = nil;
			portName = nil;
			result = noErr;
		}
		IOObjectRelease(service);
	}
	IOObjectRelease(serialPortIterator);

	if (result == noErr) {
		*outPorts = [NSArray arrayWithArray:ports];
	}
	return result;
}


/* -----------------------------------------------------------------------------
	Initialize.
----------------------------------------------------------------------------- */

- (id)init {
	if (self = [super init]) {
		NSString * prefPort;
		/*int err = */[SerialPrefsViewController preferredSerialPort:&prefPort bitRate:&baudRate];
		devPath = prefPort;
		doHandshaking = (int)[NSUserDefaults.standardUserDefaults integerForKey:@"SerialHandshake"];

		isLive = NO;
		isNegotiating = NO;
		isACKPending = NO;
		wSequence = 0;
		prevSequence = 0;
		memcpy(ltPacketHeader, kLTPacket, sizeof(kLTPacket));
		memcpy(laPacketHeader, kLAPacket, sizeof(kLAPacket));

		rPacketBuf = [[NCBuffer alloc] init];
		fGetFrameState = 0;
		rFCS = [[CRC16 alloc] init];

		wPacketBuf = [[NCBuffer alloc] init];
		wFrameBuf = [[NCBuffer alloc] init];
		wFCS = [[CRC16 alloc] init];
	}
	return self;
}


- (void)dealloc {
	devPath = nil;
	rFCS = nil;
	rPacketBuf = nil;
	wPacketBuf = nil;
	wFrameBuf = nil;
	wFCS = nil;
}


/* -----------------------------------------------------------------------------
	Open the connection.
----------------------------------------------------------------------------- */

- (NCError)listen {
	XTRY
	{
		const char *	dev;
		struct termios	options;

		dev = devPath.fileSystemRepresentation;

		// Open the serial port read/write, with no controlling terminal, and don't wait for a connection.
		// The O_NONBLOCK flag also causes subsequent I/O on the device to be non-blocking.
		// See open(2) ("man 2 open") for details.
		XFAILIF((_rfd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK)) == -1,
					REPprintf("Error opening serial port %s - %s (%d).\n", dev, strerror(errno), errno); )

		// Note that open() follows POSIX semantics: multiple open() calls to the same file will succeed
		// unless the TIOCEXCL ioctl is issued. This will prevent additional opens except by root-owned
		// processes.
		// See tty(4) ("man 4 tty") and ioctl(2) ("man 2 ioctl") for details.
		XFAILIF(ioctl(self.rfd, TIOCEXCL) == -1,
					REPprintf("Error setting TIOCEXCL on %s - %s (%d).\n", dev, strerror(errno), errno); )

		// Get the current options and save them for later reset
		XFAILIF(tcgetattr(self.rfd, &originalAttrs) == -1,
					REPprintf("Error getting tty attributes %s - %s (%d).\n", dev, strerror(errno), errno); )

		// Set raw input (non-canonical) mode, with reads blocking until either a single character 
		// has been received or a one second timeout expires.
		// See tcsetattr(4) ("man 4 tcsetattr") and termios(4) ("man 4 termios") for details.

// --> this block defines whether serial works!
#if forAdriano
		cfsetspeed(&options, baudRate);

		options.c_cc[VMIN] = 1;
		options.c_cc[VTIME] = 10;

		options.c_iflag = IGNBRK | INPCK;
		options.c_lflag = 0;
		options.c_oflag = 0;
		options.c_cflag = (CREAD | CLOCAL | CS8);
		if (doHandshaking)
			options.c_cflag |= (CCTS_OFLOW | CRTS_IFLOW);

#else
		options = originalAttrs;
		cfmakeraw(&options);
		options.c_cc[VMIN] = 0;
		options.c_cc[VTIME] = 0;

		// Set baud rate, word length, and handshake options
		cfsetspeed(&options, baudRate);
		options.c_iflag |= IGNBRK;								// ignore break (also software flow control)
		options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
		options.c_oflag &= ~OPOST;
		options.c_cflag &= ~(CSIZE | CSTOPB | PARENB);
		options.c_cflag |= (CREAD | CLOCAL | CS8);		// use 8 bit words, no parity; ignore modem control
		if (doHandshaking)
			options.c_cflag |= (CCTS_OFLOW | CRTS_IFLOW);		// KeyspanTestSerial.c uses CCTS_OFLOW | CRTS_IFLOW
																				// could also use CDSR_OFLOW | CDTR_IFLOW
																				// but nobody uses CRTSCTS
#endif
// <--

		// Set the options now
		XFAILIF(tcsetattr(self.rfd, TCSANOW, &options) == -1,
					REPprintf("\nError setting tty attributes %s - %s (%d).\n", dev, strerror(errno), errno); )

		int modem;
		XFAILIF(ioctl(self.rfd, TIOCMGET, &modem) == -1,
					REPprintf("\nError getting modem signals %s - %s (%d).\n", dev, strerror(errno), errno); )

MINIMUM_LOG {
		REPprintf("Serial port: %s, %d bps,  ", dev,baudRate);
		const char * sDCD = (modem & TIOCM_CD) ? "DCD" : "dcd";
		const char * sDTR = (modem & TIOCM_DTR)? "DTR" : "dtr";
		const char * sDSR = (modem & TIOCM_DSR)? "DSR" : "dsr";
		const char * sRTS = (modem & TIOCM_RTS)? "RTS" : "rts";
		const char * sCTS = (modem & TIOCM_CTS)? "CTS" : "cts";
		REPprintf("%s %s %s %s %s\n", sDCD, sDTR, sDSR, sRTS, sCTS);
}
#if 0
		modem |= TIOCM_DTR;
		ioctl(self.rfd, TIOCMSET, &modem);
		REPprintf("Asserting DTR.\n");
#endif

		// Success
		_wfd = self.rfd;
		return noErr;
	}
	XENDTRY;

	// Failure
	if (self.rfd >= 0) {
		close(self.rfd);
		_rfd = _wfd = -1;
	}
	return -1;
}



/* -----------------------------------------------------------------------------
	Accept the connection.
	Don’t need to do anything here -- negotiation is handled by -processPacket
----------------------------------------------------------------------------- 

- (NCError) accept
{
	return noErr;
}*/


/* -----------------------------------------------------------------------------
	Read data into a FIFO buffer queue.
	Data in the MNP protocol is packeted and framed, so we need to unframe the
	packets first and handle protocol commands.
----------------------------------------------------------------------------- */

- (NCError) readPage: (NCBuffer *) inFrameBuf into: (CChunkBuffer *) inDataBuf
{
	NCError err;
	for (err = noErr; err == noErr; ) {
		// strip packet framing
		if ((err = [self unframePacket:inFrameBuf]) == noErr)	// this drains the inFrameBuf, but might not build a whole packet
			// despatch to packet handler
			err = [self processPacket: inDataBuf];
	}
	if (err == kCommsPartialData) {
		err = noErr;
	} else if (err == kSerErrCRCError) {
		err = noErr;
		[self sendAck: NO];
	}
	return err;
}


/* -----------------------------------------------------------------------------
	Read data from the inFrameBuf (raw framed data from the wire)
	and fill the rPacketBuf (a packet in the MNP protocol).
	This MUST fully drain the inFrameBuf.
	We use an FSM to perform the unframing.
----------------------------------------------------------------------------- */

- (NCError) unframePacket: (NCBuffer *) inFrameBuf
{
	NCError status = kCommsPartialData;

	XTRY
	{
		int ch;
		for (ch = 0; ch >= 0; )		// start off w/ dummy ch
		{
			switch (fGetFrameState)
			{
			case 0:
//	scan for SYN start-of-frame char
				[rPacketBuf clear];
				[rFCS reset];
				fIsGetCharEscaped = NO;
				fIsGetCharStacked = NO;
				do
				{
					XFAIL((ch = inFrameBuf.nextChar) < 0)
					if (ch == chSYN)
						fGetFrameState = 1;
					else
						fPreHeaderByteCount++;
				} while (ch != chSYN);
				break;

//	next start-of-frame must be DLE
			case 1:
				XFAIL((ch = inFrameBuf.nextChar) < 0)
				if (ch == chDLE)
					fGetFrameState = 2;
				else
				{
					fGetFrameState = 0;
					fPreHeaderByteCount += 2;
				}
				break;

//	next start-of-frame must be STX
			case 2:
				XFAIL((ch = inFrameBuf.nextChar) < 0)
				if (ch == chSTX)
					fGetFrameState = 3;
				else
				{
					fGetFrameState = 0;
					fPreHeaderByteCount += 3;
				}
				break;

//	read char from input buffer
			case 3:
				if (fIsGetCharStacked)
				{
					fIsGetCharStacked = NO;
					ch = fStackedGetChar;
				}
				else
					XFAIL((ch = inFrameBuf.nextChar) < 0)

				if (!fIsGetCharEscaped && ch == chDLE)
					fGetFrameState = 4;
				else
				{
					rPacketBuf.nextChar = (unsigned char)ch;
					[rFCS computeCRC: ch];
					fIsGetCharEscaped = NO;
				}
				break;

// escape char
			case 4:
				XFAIL((ch = inFrameBuf.nextChar) < 0)
				if (ch == chETX)
				{
					// it’s end-of-message
					[rFCS computeCRC: ch];
					fGetFrameState = 5;
				}
				else if (ch == chDLE)
				{
					// it’s an escaped escape
					fIsGetCharStacked = YES;
					fStackedGetChar = ch;
					fIsGetCharEscaped = YES;
					fGetFrameState = 3;
				}
				else
					// it’s nonsense -- ignore it
					fGetFrameState = 3;
				break;

//	check first byte of FCS
			case 5:
				XFAIL((ch = inFrameBuf.nextChar) < 0)
				if (ch == [rFCS get: 0])
					fGetFrameState = 6;
				else
				{
					fGetFrameState = 0;
					status = kSerErrCRCError;
					ch = -1;			// fake value so we break out of the loop
				}
				break;

//	check second byte of FCS
			case 6:
				XFAIL((ch = inFrameBuf.nextChar) < 0)
				if (ch == [rFCS get: 1])
					fGetFrameState = 7;
				else
				{
					fGetFrameState = 0;
					status = kSerErrCRCError;
					ch = -1;			// fake value so we break out of the loop
				}
				break;

//	frame done
			case 7:
				// reset FSM for next time
				fGetFrameState = 0;
				status = noErr;	// noErr -- packet fully unframed
				ch = -1;				// fake value so we break out of the loop
				break;
			}
		}
	}
	XENDTRY;

	return status;
}


/* -----------------------------------------------------------------------------
	Process an MNP packet.
	We can assume rPacketBuf contains a whole packet.
----------------------------------------------------------------------------- */

- (NCError) processPacket: (CChunkBuffer *) inDataBuf
{
	NCError err = noErr;
	// start reading packet from the beginning
	[rPacketBuf reset];
	// first char is header length -- ignore it
//	int rPacketLen = rPacketBuf.ptr[0];
	// second char is packet type
	int rPacketType = rPacketBuf.ptr[1];

	switch (rPacketType)
	{
	case kLRPacketType:
		[self rcvLR];
		break;
	case kLDPacketType:
		[self rcvLD];
		err = kDockErrDisconnected;
		break;
	case kLTPacketType:
		[self rcvLT: inDataBuf];
		break;
	case kLAPacketType:
		[self rcvLA];
		break;
	case kLNPacketType:
		[self rcvLN];
		break;
	case kLNAPacketType:
		[self rcvLNA];
		break;
	default:
MINIMUM_LOG {
	REPprintf("#### received unknown packet type (%d)\n", rPacketType);
}
		// err = kCommsBadPacket;?
		// abort?
		break;
	}
	return err;
}


/* -----------------------------------------------------------------------------
	Handle a received LR (negotiation) packet.
----------------------------------------------------------------------------- */

- (void) rcvLR
{
	isLive = YES;
	isNegotiating = YES;
	rSequence = 0;
//	[self sendAck: YES];	// not necessary?
	[self sendPacket: kLRPacket data: NULL length: 0];
}


/* -----------------------------------------------------------------------------
	Handle a received LD (disconnection) packet.
----------------------------------------------------------------------------- */

- (void) rcvLD
{
MINIMUM_LOG {
	REPprintf("#### received LD packet (disconnect)\n");
}
}


/* -----------------------------------------------------------------------------
	Handle a received LT (data) packet.
	Add the data to the read queue.
----------------------------------------------------------------------------- */

- (void) rcvLT: (CChunkBuffer *) inDataBuf
{
	prevSequence = rSequence;
	rSequence = rPacketBuf.ptr[2];	// third char in header is packet sequence number

	if (rSequence == prevSequence)
	{
MINIMUM_LOG {
	NSLog(@"-[MNPSerialEndpoint rcvLT:] packet %d resent", rSequence);
}
		// must not rebuffer the data if this is a resend
	}
	else
	{
		unsigned int headerLen = 1 + rPacketBuf.ptr[0];	// first char in header is header length
		inDataBuf->write(rPacketBuf.ptr + headerLen, rPacketBuf.count - headerLen);
/*>> DEBUG >>
NSLog(@"-[MNPSerialEndpoint rcvLT:] packet %d", rSequence);
unsigned char * p = (unsigned char *)rPacketBuf.ptr + headerLen;
int plen = rPacketBuf.count - headerLen;
for (int y = 0; y < plen; y += 32)
{
	char str[32*3 + 1];
	int x;
	for (x = 0; x < 32 && y + x < plen; x++)
		sprintf(str+x*3, "%02X ", p[y+x]);
	if (x > 0)
	{
		str[x*3] = 0;
		NSLog(@"%s", str);
	}
}
<< DEBUG <<*/
	}

	[self sendAck: YES];
}


/* -----------------------------------------------------------------------------
	Handle a received LA (acknowledge) packet.
----------------------------------------------------------------------------- */

- (void) rcvLA
{
	if (isNegotiating)
	{
		isNegotiating = NO;
		wSequence = 0;
	}
	else
	{
MINIMUM_LOG {
	if (gTraceIO)
		REPprintf(rPacketBuf.ptr[3] != 0 ? "\n     <-- ACK %d" : "\n     <-- NAK %d", rPacketBuf.ptr[2]);
}
		if (rPacketBuf.ptr[3] == 1)	// ACK
			isACKPending = NO;
		else									// NAK => resend same packet
			[wFrameBuf refill];
	}
}


- (void) rcvLN
{ /* this really does nothing */ }


- (void) rcvLNA
{ /* this really does nothing */ }


/* -----------------------------------------------------------------------------
	Send an ACK packet.
----------------------------------------------------------------------------- */

- (void)sendAck:(BOOL)inOK {
MINIMUM_LOG {
	if (gTraceIO)
		REPprintf(inOK ? "\nACK %d --> " : "\nNAK %d --> ", rSequence);
}
	laPacketHeader[2] = rSequence;
	laPacketHeader[3] = inOK;
	[self sendPacket: laPacketHeader data: NULL length: 0];
}

- (void)sendLD {
	[self sendPacket:kLDPacket data:NULL length:0];
}


/* -----------------------------------------------------------------------------
	Send data from the output buffer.
	Have to break the data into 256-byte LT packet sized chunks,
	which are then framed with MNP header/trailer.
----------------------------------------------------------------------------- */

- (void) writePage: (NCBuffer *) inFrameBuf from: (NSMutableData *) inDataBuf
{
	unsigned int count;
	if (wFrameBuf.count == 0 && !isACKPending)
	{
		count = (unsigned int)inDataBuf.length;
		if (count > 0)
		{
			unsigned char packetBuf[kMNPPacketSize];
			if (count > kMNPPacketSize)
				count = kMNPPacketSize;
			[inDataBuf getBytes:packetBuf length:count];
			[inDataBuf replaceBytesInRange: NSMakeRange(0, count) withBytes: NULL length: 0];

			ltPacketHeader[2] = ++wSequence;
			[self sendPacket: ltPacketHeader data: packetBuf length: count];
			isACKPending = YES;
		}
	}
	if ((count = wFrameBuf.count) > 0)
	{
		if (count > inFrameBuf.freeSpace)
			count = inFrameBuf.freeSpace;
		[inFrameBuf fill:count from:wFrameBuf.ptr];
		[wFrameBuf drain:count];
	}
}


/* -----------------------------------------------------------------------------
	Send a packet, optionally with data.
	We can assume the data is already sub-packet-sized.
	Actually, we don’t send here, we just prepare wFrameBuf
	and say when asked that we willSend: it.
----------------------------------------------------------------------------- */

- (void) sendPacket: (const unsigned char *) inHeader data: (const unsigned char *) inBuf length: (unsigned int) inLength
{
	// Create MNP frame from packet data.

	// start the frame
	[wFrameBuf clear];
	[wFCS reset];

	// write frame start
	wFrameBuf.nextChar = chSYN;
	wFrameBuf.nextChar = chDLE;
	wFrameBuf.nextChar = chSTX;

	// copy frame header
	[self addToFrameBuf: inHeader length: 1 + inHeader[0]];

	// copy frame data
	if (inBuf != NULL)
		[self addToFrameBuf: inBuf length: inLength];

	// write frame end
	wFrameBuf.nextChar = chDLE;
	wFrameBuf.nextChar = chETX;
	[wFCS computeCRC: chETX];

	// write CRC
	wFrameBuf.nextChar = [wFCS get: 0];
	wFrameBuf.nextChar = [wFCS get: 1];

	// remember state in case we need to refill on NAK
	[wFrameBuf mark];
}


- (void)addToFrameBuf:(const unsigned char *)inBuf length:(unsigned int)inLength {
	const unsigned char * p;
	for (p = inBuf; inLength > 0; inLength--, p++) {
		const unsigned char ch = *p;
		[wFCS computeCRC: ch];
		if (ch == chDLE) {
			// escape frame end start char
			wFrameBuf.nextChar = chDLE;
		}
		wFrameBuf.nextChar = ch;
	}
}


/* -----------------------------------------------------------------------------
	Close the connection.
	Traditionally it is good practice to reset a serial port back to the state
	in which you found it.  Let's continue that tradition.
----------------------------------------------------------------------------- */

- (NCError)close {
	if (self.wfd >= 0) {
		if (isLive) {
			// Send disconnect frame.
			[self sendLD];
		}
#if 0
		// See http://blogs.sun.com/carlson/entry/close_hang_or_4028137 for an explanation of the unkillable serial app.
		// AMSerialPort say: kill pending read by setting O_NONBLOCK
		if (fcntl(_fd, F_SETFL, fcntl(_fd, F_GETFL, 0) | O_NONBLOCK) == -1)
			REPprintf("Error setting O_NONBLOCK %@ - %s(%d).\n", devPath, strerror(errno), errno);
#else
		// Block until all written output has been sent from the device.
		// Note that this call is simply passed on to the serial device driver. 
		// See tcsendbreak(3) ("man 3 tcsendbreak") for details.
		if (tcdrain(self.wfd) == -1)
//		if (tcflush(self.wfd, TCIOFLUSH) == -1)		// this may discard what we’ve just written, but tcdrain(self.wfd) blocks possibly forever if the connection is broken
			REPprintf("Error draining data - %s (%d).\n", strerror(errno), errno);
#endif

		if (tcsetattr(self.wfd, TCSANOW, &originalAttrs) == -1)
			REPprintf("Error resetting tty attributes - %s (%d).\n", strerror(errno), errno);

		close(self.wfd);	// wfd == rfd so don’t try to close that too
		_rfd = _wfd = -1;
	}
	return noErr;
}

@end
