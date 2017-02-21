
#import "AppDelegate.h"

#import <mach/mach_port.h>
#import <mach/mach_interface.h>
#import <mach/mach_init.h>

#import <IOKit/pwr_mgt/IOPMLib.h>
#import <IOKit/IOMessage.h>

io_connect_t		gRootPort;

void callback(void * x, io_service_t y, natural_t messageType, void * messageArg) {
	
	switch (messageType) {

	case kIOMessageSystemWillSleep:
		[(NCXController *)[NSApp delegate] applicationWillSleep];
		IOAllowPowerChange(gRootPort, (long) messageArg);
		break;

	case kIOMessageCanSystemSleep:
		if ([(NCXController *)[NSApp delegate] applicationCanSleep])
			IOAllowPowerChange(gRootPort, (long) messageArg);
		else
			IOCancelPowerChange(gRootPort, (long) messageArg);
		break;

/*	case kIOMessageSystemHasPoweredOn:
		REPprintf("Just had a nice snooze\n");
		[[NSApp delegate] applicationWillWake];
		break; */
	}
}



int main(int argc, const char * argv[]) {
	IONotificationPortRef notify;
	io_object_t anIterator;

	gRootPort = IORegisterForSystemPower(0, &notify, callback, &anIterator);
	CFRunLoopAddSource(CFRunLoopGetCurrent(),
							IONotificationPortGetRunLoopSource(notify),
							kCFRunLoopDefaultMode);
                        
	return NSApplicationMain(argc, argv);
}

