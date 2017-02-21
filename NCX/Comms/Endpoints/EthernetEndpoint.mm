/*
	File:		EthernetEndpoint.mm

	Contains:	Implementation of the TCP/IP ethernet endpoint.

	Written by:	Newton Research Group, 2006-2011.
*/

#import "EthernetEndpoint.h"
#import "PreferenceKeys.h"
#import "DockErrors.h"


extern BOOL gTraceIO;

/* -----------------------------------------------------------------------------
	T C P I P E n d p o i n t
----------------------------------------------------------------------------- */
@interface TCPIPEndpoint ()
{
	NSNetService * netService;
}
@end


@implementation TCPIPEndpoint

/* -----------------------------------------------------------------------------
	Check availablilty of TCP/IP endpoint.
	It’s always available.
----------------------------------------------------------------------------- */

+ (BOOL)isAvailable {
	return YES;
}


/* -----------------------------------------------------------------------------
	Initialize.
----------------------------------------------------------------------------- */

- (id)init {
	if (self = [super init]) {
		netService = nil;
	}
	return self;
}

- (void)dealloc {
	netService = nil;
}


/* -----------------------------------------------------------------------------
	Listen to a TCP socket.
----------------------------------------------------------------------------- */

- (NCError) listen
{
	NCError err = noErr;
	int fd;

	NSUserDefaults * defaults = [NSUserDefaults standardUserDefaults];
	NSInteger portNumber = [defaults integerForKey: kTCPIPPortPref];
	if (portNumber == 0)
		portNumber = kNewtonDockServicePort;

	XTRY
	{
		uint16_t chosenPort;

		// create the socket from traditional BSD socket calls,
		struct sockaddr_in serverAddress;
		socklen_t namelen = sizeof(serverAddress);
		XFAILIF((fd = socket(AF_INET, SOCK_STREAM, 0)) <= 0, err = -1;)

		memset(&serverAddress, 0, sizeof(serverAddress));
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
		serverAddress.sin_port = htons(portNumber);	// or allow the kernel to choose a random port number by passing in 0

		int status, on = 1;
		XFAILIF(status = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0, err = kDockErrDisconnected;)
		XFAILIF(status = bind(fd, (struct sockaddr *) &serverAddress, namelen) < 0, err = kDockErrDisconnected;)
		// Find out what port number was chosen.
		XFAILIF(status = getsockname(fd, (struct sockaddr *) &serverAddress, &namelen) < 0, err = kDockErrDisconnected;)
		chosenPort = ntohs(serverAddress.sin_port);

		// Once we're here, we know bind must have returned, so we can start the listen
		fcntl(fd, F_SETFL, O_NONBLOCK);
		listen(fd, 1);

		if (!netService) {
			// lazily instantiate the NSNetService object that will advertise on our behalf.
			// Passing in "" for the domain causes the service to be registered in the default registration domain,
			// which will currently always be "local".
			// Passing in "" for the name causes the service to be advertised with the computer’s name.
			netService = [[NSNetService alloc] initWithDomain:@"" type:kNewtonDockServiceType name:@"" port:chosenPort];
			netService.delegate = self;
			[netService publish];
		}
	}
	XENDTRY;
	XDOFAIL(err)
	{
		if (fd >= 0) {
			close(fd);
			fd = -1;
		}
	}
	XENDFAIL;

	_rfd = _wfd = fd;
	return err;
}


/* -----------------------------------------------------------------------------
	Accept the connection.
----------------------------------------------------------------------------- */

- (NCError)accept {
	NCError err = noErr;

// stop publishing?

	struct sockaddr_in serverAddress;
	socklen_t namelen = sizeof(serverAddress);
	int fdConnected;
	
	getsockname(self.rfd, (struct sockaddr *) &serverAddress, &namelen);
	
	if ((fdConnected = accept(self.rfd, (struct sockaddr *) &serverAddress, &namelen)) > 0) {
		close(self.rfd);
		_rfd = _wfd = fdConnected;
	} else {
		err = errno;
	}
	return err;
}


/* -----------------------------------------------------------------------------
	Disconnect.
----------------------------------------------------------------------------- */

- (NCError)close {

	if (self.rfd >= 0) {
		close(self.rfd);
		_rfd = _wfd = -1;
	}

	if (netService) {
		[netService stop];
		netService.delegate = nil;		// NSNetService bug http://openradar.appspot.com/28943305  see https://codereview.chromium.org/2445343005/
		netService = nil;
	}

	return noErr;
}


#pragma mark NSNetService

/* -----------------------------------------------------------------------------
	NSNetService delegate methods.
----------------------------------------------------------------------------- */

- (void)netServiceWillPublish:(NSNetService *)sender {
//NSLog(@"-[TCPIPEndpoint netServiceWillPublish:]");
}


- (void)netService:(NSNetService *)sender didNotPublish:(NSDictionary *)errorDict {
//NSLog(@"-[TCPIPEndpoint netService:didNotPublish:]");

	if ([[errorDict objectForKey: NSNetServicesErrorCode] intValue] == NSNetServicesCollisionError) {
		NSLog(@"A name collision occurred. A service is already running with that name someplace else.");
	} else {
		NSLog(@"Some other unknown error occurred.");
	}
	[self close];
}


- (void)netServiceDidStop:(NSNetService *)sender {
//NSLog(@"-[TCPIPEndpoint netServiceDidStop:]");
//NSLog(@"%@", [NSThread callStackSymbols]);
}

@end
