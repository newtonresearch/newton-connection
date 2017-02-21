/*
	File:		EthernetEndpoint.h

	Contains:	TCPIPEndpoint communications transport interface.

	Written by:	Newton Research Group, 2006-2011.
*/

#import "Endpoint.h"

#include <unistd.h>

// imports required for socket initialization
#include <sys/socket.h>
#include <netinet/in.h>
#undef ALIGN

//	Bonjour information
#define kNewtonDockServiceType	@"_newton-dock._tcp."
#define kNewtonDockServicePort	3679

/*------------------------------------------------------------------------------
	T C P I P E n d p o i n t
------------------------------------------------------------------------------*/

@interface TCPIPEndpoint : NCEndpoint <NSNetServiceDelegate>
//	NSNetService delegate methods
- (void)netServiceWillPublish:(NSNetService *)sender;
- (void)netService:(NSNetService *)sender didNotPublish:(NSDictionary *)errorDict;
- (void)netServiceDidStop:(NSNetService *)sender;
@end
