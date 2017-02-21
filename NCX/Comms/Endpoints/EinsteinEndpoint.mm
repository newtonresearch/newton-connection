/*
	File:		EinsteinEndpoint.mm

	Contains:	Implementation of the Einstein endpoint communications transport interface.

	Written by:	Matthias Melcher, 2017.
*/

#import "EinsteinEndpoint.h"
#import "DockErrors.h"
#import "Logging.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

extern "C" int REPprintf(const char * inFormat, ...);

/* -----------------------------------------------------------------------------
	E i n s t e i n E n d p o i n t
----------------------------------------------------------------------------- */

@interface EinsteinEndpoint ()
@end


@implementation EinsteinEndpoint

/* -----------------------------------------------------------------------------
	Check availablilty of Einstein endpoint.
	It’s always available.
	Maybe we could check whether Einstein is installed on this machine.
----------------------------------------------------------------------------- */

+ (BOOL)isAvailable {
	return YES;
}


/* -----------------------------------------------------------------------------
	Listen to a well-known pipe. Well-known by Einstein anyway.
----------------------------------------------------------------------------- */

- (NCError)listen {
	XTRY
	{
		NSURL * baseURL = [NSFileManager.defaultManager URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:NULL];
		NSURL * pipeFolder = [baseURL URLByAppendingPathComponent:@"Einstein Emulator" isDirectory:YES];
		// if folder doesn’t exist, create it
		NSError * error = NULL;
		[NSFileManager.defaultManager createDirectoryAtURL:pipeFolder withIntermediateDirectories:NO attributes:nil error:&error];
		// create the sending node if it does not exist yet
		NSURL * wPipe = [pipeFolder URLByAppendingPathComponent:@"ExtrSerPortSend"];
		const char * wPipePath = wPipe.fileSystemRepresentation;
		if (access(wPipePath, S_IRUSR|S_IWUSR) == -1) {
			XFAILIF(mkfifo(wPipePath, S_IRUSR|S_IWUSR) == -1,
				REPprintf("***** Error creating named pipe %s - %s (%d).\n", wPipePath, strerror(errno), errno); )
		}
		// create the receiving node if it does not exist yet
		NSURL * rPipe = [pipeFolder URLByAppendingPathComponent:@"ExtrSerPortRecv"];
		const char * rPipePath = rPipe.fileSystemRepresentation;
		if (access(rPipePath, S_IRUSR|S_IWUSR) == -1) {
			XFAILIF(mkfifo(rPipePath, S_IRUSR|S_IWUSR) == -1,
				REPprintf("***** Error creating named pipe %s - %s (%d).\n", rPipePath, strerror(errno), errno); )
		}

		// Open the the pipe for transmitting data from NCX to Einstein
		// The O_NONBLOCK flag also causes subsequent I/O on the device to be non-blocking.
		// Note: the name of the pipe is seen from Einsten. The receiving port must connect to the "Send" pipe.
		XFAILIF((_rfd = open(wPipePath, O_RDWR | O_NOCTTY | O_NONBLOCK)) == -1,
			REPprintf("Error opening named pipe %s for receiving - %s (%d).\n", wPipePath, strerror(errno), errno); )

		// Open the the pipe for transmitting data from NCX to Einstein
		// The O_NONBLOCK flag also causes subsequent I/O on the device to be non-blocking.
		// Note: the name of the pipe is seen from Einsten. The sneding port must connect to the "Recv" pipe.
		XFAILIF((_wfd = open(rPipePath, O_RDWR | O_NOCTTY | O_NONBLOCK)) == -1,
			REPprintf("Error opening named pipe %s for transmitting - %s (%d).\n", rPipePath, strerror(errno), errno); )

		REPprintf("Listening to Einstein connection via named pipes.\n");
		return noErr;
	}
	XENDTRY;

	if (self.rfd != -1) {
		close(self.rfd);
		_rfd = -1;
	}
	if (self.wfd != -1) {
		close(self.wfd);
		_wfd = -1;
	}
	return errno;
}


/* -----------------------------------------------------------------------------
	Disconnect.
----------------------------------------------------------------------------- */

- (NCError)close {

	if (isLive) {
		// Send disconnect frame.
		[super sendLD];
		// The LD Block will be buffered in the pipe. No need to wait until the transfer is done.
	}

	if (self.wfd >= 0) {
		close(self.wfd);
		_wfd = -1;
	}

	if (self.rfd >= 0) {
		close(self.rfd);
		_rfd = -1;
	}

	return noErr;
}


@end
