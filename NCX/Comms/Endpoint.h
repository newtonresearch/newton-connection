/*
	File:		Endpoint.h

	Contains:	Communications endpoint and controller interfaces.

	Written by:	Newton Research Group, 2011.
*/

#import <dispatch/dispatch.h>
#include <sys/select.h>

#import "Comms.h"
#import "Chunks.h"
#import "NCBuffer.h"

// from "Newton/NewtonDebug.h"

#define XTRY							do
#define XENDTRY  						while(0)

#define XDOFAIL(expr)				if (expr) do
#define XENDFAIL						while(0)

#define XFAIL(expr)					{ if ((expr) != 0) { break; } }
#define XFAILIF(expr, action)		{ if ((expr) != 0) { { action } break; } }
#define XFAILNOT(expr, action)	{ if ((expr) == 0) { { action } break; } }


/* -----------------------------------------------------------------------------
	Values that can be passed to setTimeout as timeout values.
----------------------------------------------------------------------------- */

#define kCommsDefaultTimeout		(-1)		/* 30 seconds			*/
#define kCommsNoTimeout				(-2)		/* many more seconds	*/

#define kDefaultTimeoutInSecs		 30


/* -----------------------------------------------------------------------------
	N C E n d p o i n t
	Base class.
	Subclass
		TCPIPEndpoint
		MNPSerialEndpoint
		EinsteinEndpoint
		BluetoothEndpoint
----------------------------------------------------------------------------- */
@class NCDockEventQueue;

@interface NCEndpoint : NSObject
{
	int _rfd, _wfd;					// every endpoint must be based on a file descriptor since the GCD dispatch source requires it
	int timeoutSecs;					// timeout in seconds
//	dispatch_source_t readSrc;		// GCD dispatch source for reading data from fd
	NCBuffer * rPageBuf;				// 1K buffer into which to read fd data
	CChunkBuffer rData;				// buffer into which to read unframed data

	dispatch_queue_t ioQueue;		// async serial dispatch queue in which to perform i/o
	dispatch_queue_t evtQueue;		// async serial dispatch queue in which to build dock events
	dispatch_semaphore_t syncWrite;

//	dispatch_source_t writeSrc;	// GCD dispatch source for writing data to fd
	NCBuffer * wPageBuf;				// 1K buffer into which to write fd data
	NSMutableData * wData;
	BOOL isSyncWrite;
}

@property(nonatomic,readonly) int rfd;		// read file descriptor
@property(nonatomic,readonly) int wfd;		// write file descriptor
@property(nonatomic,assign) int pipefd;
@property(nonatomic,assign) int timeout;
@property(nonatomic,weak) NCDockEventQueue * eventQueue;

// public interface
+ (BOOL)isAvailable;

- (NCError)write:(const void *)inData length:(unsigned int)inLength;
- (NCError)writeSync:(const void *)inData length:(unsigned int)inLength;
- (BOOL)willWrite;
- (void)writeDone;

// subclass repsonsibility
- (NCError)listen;
- (NCError)accept;
- (NCError)readPage:(NCBuffer *)inFrameBuf into:(CChunkBuffer *)inDataBuf;
- (void)writePage:(NCBuffer *)inFrameBuf from:(NSMutableData *)inDataBuf;
- (NCError)close;

// private
- (NCError)readDispatchSource;
- (NCError)writeDispatchSource;

@end


/* -----------------------------------------------------------------------------
	N C E n d p o i n t C o n t r o l l e r
	There is a single instance of NCEndpointController that coordinates the
	creation of transports to listen on all available interfaces, then cancels
	transports once one has established a connection.
----------------------------------------------------------------------------- */

@interface NCEndpointController : NSObject
@property(nonatomic,readonly) NCEndpoint * endpoint;
@property(nonatomic,readonly) BOOL isActive;
@property(nonatomic,assign) int error;

- (NCError)startListening;
- (void)suppressTimeout:(BOOL)inDoSuppress;
- (void)stop;
@end
