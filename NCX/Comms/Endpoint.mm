/*
	File:		Endpoint.mm

	Contains:	Communications endpoint controller implementation.

	Written by:	Newton Research Group, 2011.
*/

#import "DockEventQueue.h"
#import "DockErrors.h"
#import "Logging.h"

// we need to know all available transports
#import "EthernetEndpoint.h"
#import "MNPSerialEndpoint.h"
#import "EinsteinEndpoint.h"
//#import "BluetoothEndpoint.h"


/* -----------------------------------------------------------------------------
	D a t a
----------------------------------------------------------------------------- */

BOOL gTraceIO = NO;

@protocol NCDockEventProtocol
- (void)readEvent:(CChunkBuffer *)inData;
@end


/* -----------------------------------------------------------------------------
	N C E n d p o i n t
----------------------------------------------------------------------------- */
@implementation NCEndpoint

+ (BOOL) isAvailable {
	return NO;
}

/*------------------------------------------------------------------------------
	Initialize instance.
------------------------------------------------------------------------------*/

- (id) init {
	if (self = [super init]) {
		_rfd = _wfd = -1;
		timeoutSecs = kDefaultTimeoutInSecs;

		rPageBuf = [[NCBuffer alloc] init];
//		rData = new CChunkBuffer;		// actually it’s static

		wPageBuf = [[NCBuffer alloc] init];
		wData = [[NSMutableData alloc] init];

		syncWrite = dispatch_semaphore_create(0);
		isSyncWrite = NO;

		ioQueue = dispatch_queue_create("com.newton.connection.io", NULL);
	}
	return self;
}


- (int)rfd {
	return _rfd;
}

- (int)wfd {
	return _wfd;
}


/*------------------------------------------------------------------------------
	timeout property accessors
------------------------------------------------------------------------------*/

- (void)setTimeout:(int)inTimeout {
	if (inTimeout < -2)
		return;	// kNCInvalidParameter

	timeoutSecs = (inTimeout == -1) ? kDefaultTimeoutInSecs : inTimeout;
	return;		// noErr
}


- (int)timeout {
	return timeoutSecs;
}


/*------------------------------------------------------------------------------
	Wait for first data to arrive on this endpoint.
	Args:		--
	Return:	error code
------------------------------------------------------------------------------*/

- (NCError) listen {
	return noErr;	// subclass responsibility
}


/*------------------------------------------------------------------------------
	Accept connection.
	Args:		--
	Return:	error code
------------------------------------------------------------------------------*/

- (NCError) accept {
	return noErr;	// subclass responsibility
}


/*------------------------------------------------------------------------------
	Read from the file descriptor.
	Unframe that data (if necessary: think MNP serial) and pass it to the dock
	event queue to build into a dock event.
	Args:		--
	Return:	error code
------------------------------------------------------------------------------*/

- (NCError)readDispatchSource {
	NCError err = noErr;
//int cnt = rPageBuf.count;
//if (rPageBuf.freeSpace == 0) REPprintf("-[NCEndpoint readDispatchSource] rPageBuf.freeSpace == 0\n");
	// read() into a 1K buffer, and pass it to the transport for unframing/packetising
	int count = (int)read(_rfd, rPageBuf.ptr, rPageBuf.freeSpace);
	if (count > 0) {

MINIMUM_LOG {
	if (gTraceIO) {
		REPprintf("<<");
		for (int i = 0; i < count; ++i) REPprintf(" %02X", rPageBuf.ptr[i]);
		REPprintf("\n");
	}
}

		[rPageBuf fill:count];
		err = [self readPage:rPageBuf into:&rData];
		if (err == noErr && rData.size() > 0) {
			[NCDockEventQueue.sharedQueue readEvent:&rData];
		}
	} else if (count == 0) {
		err = kDockErrDisconnected;
		err = noErr; // FIXME: matt: be relaxed about this. Maybe the next package goes through?
	} else {
		err = kDockErrDesktopError;
	}
//if (cnt != rPageBuf.count) REPprintf("-[NCEndpoint readDispatchSource] rPageBuf.count %d -> %d\n",cnt,rPageBuf.count);
	return err;
}


/*------------------------------------------------------------------------------
	Process raw framed/packetised data from the fd into plain data.
	If the subclass needs no processing we can use this method which copies
	frame -> data.
	Args:		inFrameBuf		raw data from the fd ->
				inDataBuf		-> unframed user data
	Return:	--
				inFrameBuf MUST be drained of whatever was unframed
------------------------------------------------------------------------------*/

- (NCError)readPage:(NCBuffer *)inFrameBuf into:(CChunkBuffer *)inDataBuf {
	unsigned int count = inDataBuf->write(inFrameBuf.ptr, inFrameBuf.count);
	[inFrameBuf drain:count];
	return noErr;
}


/*------------------------------------------------------------------------------
	Write to the file descriptor.
	Frame that data (if necessary: think MNP serial) before writing.
	Args:		--
	Return:	error code
------------------------------------------------------------------------------*/

- (BOOL)willWrite {
	BOOL __block willDo = NO;
	dispatch_sync(ioQueue, ^{
		[self writePage:wPageBuf from:wData];
		willDo = wPageBuf.count > 0;
	});
	return willDo;
}


- (NCError)writeDispatchSource {
	NCError err = noErr;
	// fetch a frame from the buffer and write() it
	if (wPageBuf.count > 0) {
		int count = (int)write(_wfd, wPageBuf.ptr, wPageBuf.count);
		if (count > 0) {

MINIMUM_LOG {
	if (gTraceIO) {
		REPprintf(">>");
		for (int i = 0; i < count; ++i) REPprintf(" %02X", wPageBuf.ptr[i]);
		REPprintf("\n");
	}
}

			[wPageBuf drain:count];
			[self writeDone];
		} else if (count == 0) {
			err = kDockErrDisconnected;
		} else {	// count < 0 => error
			if (errno != EAGAIN && errno != EINTR) {
				err = kDockErrDesktopError;
			}
		}
	}
	return err;
}


/*------------------------------------------------------------------------------
	Copy data from data buffer to output frame buffer.
	Args:		inFrameBuf		framed data to be written to fd <-
				inDataBuf		<- user data to be sent
	Return:	--
				inDataBuf MUST be drained of whatever was sent
------------------------------------------------------------------------------*/

- (void)writePage:(NCBuffer *)inFrameBuf from:(NSMutableData *)inDataBuf {
	unsigned int count = (unsigned int)inDataBuf.length;
	if (count > inFrameBuf.freeSpace) {
		count = inFrameBuf.freeSpace;
	}
	[inFrameBuf fill:count from:[inDataBuf bytes]];
	[inDataBuf replaceBytesInRange: NSMakeRange(0, count) withBytes: NULL length: 0];
}


/*------------------------------------------------------------------------------
	Public interface: write data to the endpoint.
	Args:		inData
				inLength
	Return:	error code
------------------------------------------------------------------------------*/

- (NCError)write:(const void *)inData length:(unsigned int)inLength {
	NCError err = noErr;
	if (inData) {
		NSData * data = nil;
		if (inLength) {
			data = [NSData dataWithBytes:inData length:inLength];
		}
		dispatch_sync(ioQueue, ^{
			BOOL wasEmpty = wData.length == 0;
			if (data) {
				[wData appendData:data];
			}
			if (wasEmpty) {
				write(self.pipefd, "X", 1);
			}
		});
	}
	return err;
}


/*------------------------------------------------------------------------------
	Public interface: write data to the endpoint.
	Args:		inData
				inLength
	Return:	error code
------------------------------------------------------------------------------*/

- (NCError)writeSync:(const void *)inData length:(unsigned int)inLength {
	NCError err = noErr;
	if (inData) {
		NSData * data = [NSData dataWithBytes:inData length:inLength];
		dispatch_sync(ioQueue, ^{
			BOOL wasEmpty = wData.length == 0;
			[wData appendData:data];
			if (wasEmpty) {
				isSyncWrite = YES;
				write(self.pipefd, "Y", 1);
			}
		});
		dispatch_semaphore_wait(syncWrite, DISPATCH_TIME_FOREVER);
	}
	return err;
}


- (void)writeDone {
	dispatch_sync(ioQueue, ^{
		if (isSyncWrite && wData.length == 0) {
			isSyncWrite = NO;
			dispatch_semaphore_signal(syncWrite);
		}
	});
}


/*------------------------------------------------------------------------------
	Close this endpoint.
	Args:		--
	Return:	error code
------------------------------------------------------------------------------*/

- (NCError)close {
	return noErr;	// subclass responsibility
}

@end


/*------------------------------------------------------------------------------
	N C E n d p o i n t C o n t r o l l e r
------------------------------------------------------------------------------*/
@interface NCEndpointController ()
{
	NSMutableArray<NCEndpoint *> * listeners;
	NCEndpoint * _endpoint;
	int pipefd[2];		// safe-pipe trick
	int timeoutSuppressionCount;
}
- (NCError)addEndpoint:(NCEndpoint *)inEndpoint name:(const char *)inName;
- (NCError)useEndpoint:(NCEndpoint *)inEndpoint;
- (void)doIOEventLoop;
@end

@implementation NCEndpointController
@synthesize error;

/*------------------------------------------------------------------------------
	Initialize instance.
	Create endpoints for all transports we know about and start listening for
	data.
------------------------------------------------------------------------------*/

- (id)init {
	if (self = [super init]) {
		listeners = nil;
		_endpoint = nil;
		self.error = noErr;
		[self suppressTimeout:NO];
	}
	return self;
}


- (BOOL)isActive {
	return listeners != nil || _endpoint != nil;
}


- (void)stop {
	if (self.isActive) {
		write(pipefd[1], "Z", 1);
	}
	if (_endpoint) {
		[_endpoint close];
		_endpoint = nil;
	} else {
		[self useEndpoint:nil];
	}
}


/*------------------------------------------------------------------------------
	Dispose instance.
	Close all active endpoints.
------------------------------------------------------------------------------*/

- (void)dealloc {
	[self stop];
}


/*------------------------------------------------------------------------------
	Start listening on all available transports, and accept whichever connects
	first.
	Can’t use kevent() to check whether serial port ready to read -- it just returns an EINVAL error.
	Can’t use GCD dispatch sources -- they’re based on kevent.
	Only remains select().
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/

- (NCError)startListening {
	NCError err;
	self.error = noErr;

	XTRY
	{
		NCEndpoint * ep;

		// create all available endpoints
		listeners = [[NSMutableArray alloc] initWithCapacity:3];

		if ([MNPSerialEndpoint isAvailable]) {
			ep = [[MNPSerialEndpoint alloc] init];
			err = [self addEndpoint:ep name:"MNP serial"];
		}

		if ([EinsteinEndpoint isAvailable]) {
			ep = [[EinsteinEndpoint alloc] init];
			err = [self addEndpoint:ep name:"Einstein"];
		}

		if ([TCPIPEndpoint isAvailable]) {
			ep = [[TCPIPEndpoint alloc] init];
			err = [self addEndpoint:ep name:"ethernet"];
		}
/*
		if ([BluetoothEndpoint isAvailable]) {
			ep = [[BluetoothEndpoint alloc] init];
			err = [self addEndpoint:ep name:"bluetooth"];
		}
*/
	}
	XENDTRY;

	// start I/O event loop in a parallel dispatch queue
	NCEndpointController *__weak weakself = self;
	dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		[weakself doIOEventLoop];
	});

	return err;
}


- (void)doIOEventLoop {
	int err;
	NCEndpoint * ep = nil;	// => is listening; once connected, ep is the current endpoint
	int maxfd;

	pipe(pipefd);	// create write-signal pipe

	// this is our I/O event loop
	for (err = noErr; err == noErr; ) {
		int count;
		int nfds;
		fd_set rfds;
		fd_set wfds;
		struct timeval tv;

		if (ep == nil) {
			// we’re listening…
			FD_ZERO(&rfds);
			maxfd = 0;
			for (NCEndpoint * epi in listeners)
			{
				FD_SET(epi.rfd, &rfds);
				maxfd = MAX(epi.rfd, maxfd);
			}
			nfds = select(maxfd+1, &rfds, NULL, NULL, NULL);

		} else {
			tv.tv_sec = [ep timeout];
			tv.tv_usec = 0;

			FD_ZERO(&rfds);
			FD_SET(ep.rfd, &rfds);
			FD_SET(pipefd[0], &rfds);
			maxfd = MAX(pipefd[0], maxfd);

			FD_ZERO(&wfds);
			// cf linuxmanpages: only set the wfds if there are data to be sent
			if ([ep willWrite]) {
				FD_SET(ep.wfd, &wfds);
				maxfd = MAX(ep.wfd, maxfd);
			}
			// wait for an event on read OR write file descriptor
			nfds = select(maxfd+1, &rfds, &wfds, NULL, &tv);
		}

		if (nfds > 0) {	// socket is available
			if (ep == nil) {
				// we were listening… find the endpoint that connected
				for (NCEndpoint * epi in listeners) {
					if (FD_ISSET(epi.rfd, &rfds)) {
						// accept this connection, cancel other listener transports
						ep = epi;
						[self useEndpoint:ep];
						break;
					}
				}
				// set write-signal pipe in endpoint
				ep.pipefd = pipefd[1];
				// go on to process the data just received
			}

			if (FD_ISSET(ep.rfd, &rfds)) {
				// read() into frame buffer, unframe into data buffer, build dock event from data
				err = [ep readDispatchSource];
//if (err) REPprintf("-[NCEndpointController doIOEventLoop] readDispatchSource -> error %d\n",err);
			}

			if (FD_ISSET(pipefd[0], &rfds)) {
				// endpoint signalled write
				char	x[2];
				count = (int)read(pipefd[0], x, 1);
				if (x[0] == 'Z') {
					err = kDockErrDisconnected;
				}
			}

			if (FD_ISSET(ep.wfd, &wfds)) {
				// we can write
				err = [ep writeDispatchSource];
//if (err) REPprintf("-[NCEndpointController doIOEventLoop] writeDispatchSource -> error %d\n",err);
			}
		} else if (nfds == 0) {	// timeout
			if (--timeoutSuppressionCount < 0) {
//REPprintf("select(): timeout\n");
				err = kDockErrIdleTooLong;
			} else {
				err = noErr;	// pretend it did not happen
			}
		} else {	// nfds < 0: error
//REPprintf("select(): %d, errno = %d\n", nfds, errno);
			if (nfds == -1 && errno == EINTR)
				continue;	// we were interrupted -- ignore it
			err = kDockErrDisconnected;	// because there are no comms after we break
		}
	}
	self.error = err;
}


/*------------------------------------------------------------------------------
	Add an endpoint to our list of listeners, and start listening.
	Args:		inEndpoint
				inName
	Return:	error code
------------------------------------------------------------------------------*/

- (NCError)addEndpoint:(NCEndpoint *)inEndpoint name:(const char *)inName {
	NCError err = noErr;
	XTRY
	{
		[listeners addObject:inEndpoint];
		// listen for data on this endpoint == connection
		XFAIL(err = [inEndpoint listen])
	}
	XENDTRY;
	XDOFAIL(err)
	{
		[inEndpoint close];
		[listeners removeObject:inEndpoint];
		REPprintf("Not listening on %s connection: error %d.\n", inName, err);
	}
	XENDFAIL;
	return err;
}


/*------------------------------------------------------------------------------
	Use an endpoint for further comms. Cancel any other outstanding listeners.
	Args:		inEndpoint
	Return:	error code
------------------------------------------------------------------------------*/

- (NCError)useEndpoint:(NCEndpoint *)inEndpoint {
	for (NCEndpoint * ep in listeners) {
		if (ep == inEndpoint) {
			[ep accept];
		} else {
			[ep close];
		}
	}
	_endpoint = inEndpoint;
	[listeners removeAllObjects];
	listeners = nil;
	return noErr;
}


/*------------------------------------------------------------------------------
	Return the active endpoint.
	Args:		--
	Return:	the endpoint
				nil => no connection
------------------------------------------------------------------------------*/

- (NCEndpoint *)endpoint {
	return _endpoint;
}


/*------------------------------------------------------------------------------
	Suppress communications timeout.
	We need to do this for keyboard passthrough and screenshot functions, since
	there is no protocol exchange while waiting for user action.
	Args:		inDoSuppress
	Return:	--
 ------------------------------------------------------------------------------*/

- (void)suppressTimeout:(BOOL)inDoSuppress {
	timeoutSuppressionCount = inDoSuppress ? INT32_MAX : 1;
}

@end
