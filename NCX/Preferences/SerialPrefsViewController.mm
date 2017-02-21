/*
	File:		SerialPrefsViewController.m

	Contains:	Serial preferences view controller for the NCX app.

	Written by:	Newton Research Group, 2015.
*/

#import "SerialPrefsViewController.h"
#import "MNPSerialEndpoint.h"
#import "PreferenceKeys.h"


@interface SerialPrefsViewController ()
{
//	serial prefs
	NSArray * ports;
	NSString * serialPort;
	NSUInteger serialSpeed;
	NSString * originalPort;
	NSUInteger originalSpeed;
}
@end

@implementation SerialPrefsViewController

/*------------------------------------------------------------------------------
	Create the UI.
------------------------------------------------------------------------------*/

- (void)viewDidLoad {
	ports = nil;
	serialPort = nil;

	if ([MNPSerialEndpoint isAvailable]
	&&  [MNPSerialEndpoint getSerialPorts:&ports] == noErr
	&&  ports.count > 0) {
		[SerialPrefsViewController preferredSerialPort:&serialPort bitRate:&serialSpeed];

		// load it up with available serial port names
		NSUInteger i, count = ports.count;
		NSUInteger serialPortIndex = 999;
		[serialPortPopup removeAllItems];
		for (i = 0; i < count; ++i) {
			NSString * port = [ports[i] objectForKey:@"name"];
			[serialPortPopup addItemWithTitle:port];
			port = [ports[i] objectForKey:@"path"];
			if ([port isEqualToString: serialPort])
				serialPortIndex = i;
		}
		if (serialPortIndex == 999) {
			// serialPort isn’t known by IOKit -- maybe user has set their own default
			// add name, path couplet to ports
			NSMutableArray * newPorts = [NSMutableArray arrayWithArray:ports];
			[newPorts addObject: @{ @"name":serialPort, @"path":serialPort }];
			ports = [[NSArray alloc] initWithArray:newPorts];
			[serialPortPopup addItemWithTitle:serialPort];
			serialPortIndex = count;
		}
		[serialPortPopup selectItemAtIndex:serialPortIndex];
		originalPort = serialPort;
		originalSpeed = serialSpeed;
	}
}


/*------------------------------------------------------------------------------
	Called when the receiver is about to close.
	If the serial port has been changed, tell the app so it can reset the
	connection using the new port.
	Args:		notification
	Return:	--
------------------------------------------------------------------------------*/

- (void)viewWillDisappear {
	// only need to do anything if we have a serial port
	if (serialPort != nil) {
		NSUserDefaults * sharedUserDefaults = NSUserDefaults.standardUserDefaults;
		serialPort = [sharedUserDefaults stringForKey:kSerialPortPref];
		serialSpeed = [sharedUserDefaults integerForKey:kSerialBaudPref];
		if (![serialPort isEqualToString:originalPort] || serialSpeed != originalSpeed) {
			originalPort = serialPort;
			originalSpeed = serialSpeed;
			[NSNotificationCenter.defaultCenter postNotificationName:kSerialPortChanged
																			  object:self
																			userInfo:nil];
		}
	}
}


#pragma mark General
/*------------------------------------------------------------------------------
	S e r i a l   P r e f e r e n c e s
------------------------------------------------------------------------------*/

+ (int)preferredSerialPort:(NSString *__strong *)outPort bitRate:(NSUInteger *)outRate {
	NSUserDefaults * sharedUserDefaults = NSUserDefaults.standardUserDefaults;
	NSString * serialPort = [sharedUserDefaults stringForKey:kSerialPortPref];
	NSArray * ports;
	int count;

	if (serialPort == nil) {
		// there’s no serial port preference -- use the first one available
		if ([MNPSerialEndpoint isAvailable]
		&&  [MNPSerialEndpoint getSerialPorts:&ports] == noErr
		&&  (count = (int)ports.count) > 0) {
			serialPort = ports[0][@"path"];
			[sharedUserDefaults setObject:serialPort forKey:kSerialPortPref];
		}
	} else {
		NSNumberFormatter * f = [[NSNumberFormatter alloc] init];
		[f setAllowsFloats:NO];
		if ([f numberFromString:serialPort] != nil) {
			// serial port pref is numeric, convert it to the string in ports[that index].path and write it back out
			if ([MNPSerialEndpoint isAvailable]
			&&  [MNPSerialEndpoint getSerialPorts:&ports] == noErr
			&&  (count = (int)ports.count) > 0) {
				int i = serialPort.intValue;
				serialPort = ports[i][@"path"];
				[sharedUserDefaults setObject:serialPort forKey:kSerialPortPref];
			}
		}
	}
//	else assume we’ve got the device path
	*outPort = serialPort;

	NSUInteger rate = [sharedUserDefaults integerForKey:kSerialBaudPref];
	if (rate == 0)
		rate = 38400;
	*outRate = rate;

	return 0;	// ought to return error code if appropriate
}


/*------------------------------------------------------------------------------
	Keep the serial port default in sync with the UI.
	Args:		sender		the user interface element
	Return:	--
------------------------------------------------------------------------------*/

- (IBAction)updateSerialPort:(id)sender
{
	NSInteger i = ((NSPopUpButton *)sender).indexOfSelectedItem;
	if (i >= 0) {
		NSString * port = ports[i][@"path"];
		[self.sharedUserDefaults setObject:port forKey:kSerialPortPref];
	}
}

@end
