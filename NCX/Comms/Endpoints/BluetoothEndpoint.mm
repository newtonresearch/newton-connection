/*
	File:		BluetoothEndpoint.mm

	Contains:	Implementation of the bluetooth endpoint.

	Written by:	Newton Research Group, 2006.
*/

#import "BluetoothEndpoint.h"


/*------------------------------------------------------------------------------
	B l u e t o o t h E n d p o i n t
------------------------------------------------------------------------------*/

@implementation BluetoothEndpoint

/*------------------------------------------------------------------------------
	Initialize.
------------------------------------------------------------------------------*/

- (id) init
{
	if (self = [super init])
	{
		bluetoothService = nil;
		getQueue = nil;
	}
	return self;
}


/*------------------------------------------------------------------------------
	Listen for a bluetooth device.
------------------------------------------------------------------------------*/

- (NCError) listen
{
	if (!bluetoothService)
	{
		// instantiate the BluetoothService object that will advertise on our behalf.
		bluetoothService = [[BluetoothService alloc] init];
		[bluetoothService setDelegate: self];
		[bluetoothService publish: [[NSBundle bundleForClass: [self class]] pathForResource: @"BluetoothService" ofType: @"plist"]];
	}
	return noErr;
}


/*------------------------------------------------------------------------------
	Accept the connection.
	No need to do anything here; bluetooth service stops publishing when it
	opens its channel
------------------------------------------------------------------------------*/

- (NCError) accept
{
	return noErr;
}


/*------------------------------------------------------------------------------
	Read a block of data.
------------------------------------------------------------------------------*/

- (void) queueData: (void *) inData length: (unsigned int) inLength
{
	// this is the callback from the BluetoothService
	// we need to buffer the data...
	if (getQueue)
		[getQueue write: inData length: inLength];
}

- (NCError)	read: (NCChunkBuffer *) inGetQueue
{
	if (!getQueue)
		getQueue = inGetQueue;
	// wait for BluetoothService to fill the queue
	return noErr;
}


/*------------------------------------------------------------------------------
	Write a block of data.
------------------------------------------------------------------------------*/

- (NCError)	write: (const void *) inData length: (unsigned int *) ioLength
{
	NCError err = noErr;
	IOReturn result;

	if ((result = [bluetoothService write: inData length: *ioLength]) != kIOReturnSuccess)
		err = kDockErrDesktopError;	// errno => specific error
	return err;
}


/*------------------------------------------------------------------------------
	Disconnect.
------------------------------------------------------------------------------*/

- (NCError) disconnect
{
	if (bluetoothService)
	{
		[bluetoothService stop];
		[bluetoothService disconnect];
	}
	return noErr;
}


/*------------------------------------------------------------------------------
	Delegate methods.
------------------------------------------------------------------------------*/

- (void) bluetoothDeviceDidConnect
{
	// nothing to do here -- wait for first protocol data to arrive
}

- (void) bluetoothDeviceDidDisconnect
{
	// need to report this to a higher authority
}

@end


/*------------------------------------------------------------------------------
	B l u e t o o t h S e r v i c e
------------------------------------------------------------------------------*/

@implementation BluetoothService

/*------------------------------------------------------------------------------
	Set a delegate object to receive callbacks.
	Args:		inDelegate
	Return:	--
------------------------------------------------------------------------------*/

- (void) setDelegate: (id) inDelegate
{
	delegate = inDelegate;
}


/*------------------------------------------------------------------------------
	Publish bluetooth services; add the service to the SDP dictionary.
	Args:		inDictionaryPath
	Return:	--
------------------------------------------------------------------------------*/

- (void) publish: (NSString *) inDictionaryPath
{
	if (inDictionaryPath != nil)
	{
		NSDictionary * sdpEntries = [NSDictionary dictionaryWithContentsOfFile: inDictionaryPath];
		if (sdpEntries != nil)
		{
			IOBluetoothSDPServiceRecordRef serviceRecordRef;
			if (IOBluetoothAddServiceDict((CFDictionaryRef) sdpEntries, &serviceRecordRef ) == kIOReturnSuccess)
			{
				IOBluetoothSDPServiceRecord * serviceRecord = [IOBluetoothSDPServiceRecord withSDPServiceRecordRef: serviceRecordRef];
				[serviceRecord getRFCOMMChannelID: &theChannelId];
				[serviceRecord getServiceRecordHandle: &serviceHandle];

				IOBluetoothObjectRelease(serviceRecordRef);

				// Register a notification so we get notified when an incoming RFCOMM channel is opened
				// on the channel assigned to our service.
				channelOpenNotification = [IOBluetoothRFCOMMChannel registerForChannelOpenNotifications: self
																					 selector: @selector(rfcommChannelOpened:channel:)
																					 withChannelID: theChannelId
																					 direction: kIOBluetoothUserNotificationChannelDirectionIncoming];
			}
		}
	}
}


- (void) rfcommChannelOpened: (IOBluetoothUserNotification *) inNotification channel: (IOBluetoothRFCOMMChannel *) inChannel
{
	theChannel = inChannel;
			
	// Set self as the channel's delegate: THIS IS THE VERY FIRST THING TO DO FOR A SERVER !!!!
	if ([theChannel setDelegate: self] == kIOReturnSuccess)
	{                
		// Stop providing the services.
		// This app only handles one Newton connection at a time -
		// but there’s no reason a well written app can’t handle any number of connections.
		[self stop];

		// notify our client that we have a new connection
		[delegate bluetoothDeviceDidConnect];
	}
	else
	{
		// The setDelegate: call failed. This is catastrophic.
		theChannel = nil;
	}
}


/*------------------------------------------------------------------------------
	Stop publishing our bluetooth service; remove the published service from the
	SDP dictionary.
	If a connection is in progress it will not be interrupted.
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/

- (void) stop
{
	// remove the service
	if (serviceHandle != 0)
	{
		IOBluetoothRemoveServiceWithRecordHandle(serviceHandle);
	}
	// unregister the notification
	if (channelOpenNotification != nil)
	{
		[channelOpenNotification unregister];
		channelOpenNotification = nil;
	}
	theChannelId = 0;
}


/*------------------------------------------------------------------------------
	Return the name of the device we are connected to.
	Args:		--
	Return:	an auto-released string
				or nil if there is no connection at present
------------------------------------------------------------------------------*/

- (NSString *) remoteDeviceName
{
	return (theChannel != nil)
			? [[theChannel getDevice] getName]
			: nil;
}


/*------------------------------------------------------------------------------
	Return the name of the local bluetooth device.
	Args:		--
	Return:	an auto-released string
------------------------------------------------------------------------------*/

- (NSString *) localDeviceName
{
	BluetoothDeviceName localDeviceName;
	return (IOBluetoothLocalDeviceReadName(localDeviceName, NULL, NULL, NULL) == kIOReturnSuccess)
			? [NSString stringWithUTF8String: (const char *) localDeviceName]
			: nil;
}


/*------------------------------------------------------------------------------
	Send data.
	Args:		inQueue
	Return:	error code
------------------------------------------------------------------------------*/
#define kSendPacketSize		512

- (int) write: (const void *) inData length: (unsigned int) inLength
{
	if (theChannel != nil)
	{
		IOReturn result = kIOReturnSuccess;

		// Get the RFCOMM Channel's MTU.
		// Each write can only contain up to the MTU size number of bytes.
		BluetoothRFCOMMMTU packetSize = [theChannel getMTU];

		// Loop through the data until we have no more to send.
		int lengthDone = 0, lengthRemaining;
		for (lengthRemaining = inLength; lengthRemaining > 0 && result == kIOReturnSuccess; lengthDone += packetSize, lengthRemaining -= packetSize)
		{
			if (packetSize > lengthRemaining)
				packetSize = lengthRemaining;
			// This method won't return until the buffer has been passed to the Bluetooth hardware to be sent to the remote device.
			// Alternatively, the asynchronous version of this method could be used which would queue up the buffer and return immediately.
			result = [theChannel writeSync: (char *)inData+lengthDone length: packetSize];
		}
		return result;
	}
	return kIOReturnError;
}


/*------------------------------------------------------------------------------
	Disconnect;  close the channel.
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/

- (void) disconnect
{
	if (theChannel != nil)
	{
		IOBluetoothDevice * device = [theChannel getDevice];
		// close the channel
		[theChannel closeChannel];
		theChannel = nil;
		// close the connection with the device
		[device closeConnection];
	}
}


/*------------------------------------------------------------------------------
	We have said we are the delegate for IOBluetoothRFCOMMChannelDelegate
	protocol callbacks (see IOBluetoothRFCOMMChannel.h).
	We implement only a couple of them.
------------------------------------------------------------------------------*/

// data arrived
- (void) rfcommChannelData: (IOBluetoothRFCOMMChannel *) inChannel data: (void *) inData length: (unsigned int) inLength
{
	[delegate queueData: inData length: inLength];
}


// the remote end closed the connection
- (void) rfcommChannelClosed: (IOBluetoothRFCOMMChannel *) inChannel
{
	[delegate bluetoothDeviceDidDisconnect];
}

@end
