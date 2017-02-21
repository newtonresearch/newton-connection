/*
	File:		BluetoothEndpoint.h

	Contains:	BluetoothEndpoint communications transport interface.

	Written by:	Newton Research Group, 2006.
*/

#import "Endpoint.h"

// Bluetooth Objective C Headers:
#import <IOBluetooth/objc/IOBluetoothDevice.h>
#import <IOBluetooth/objc/IOBluetoothSDPServiceRecord.h>
#import <IOBluetooth/objc/IOBluetoothRFCOMMChannel.h>

// Bluetooth C Headers:
#include <IOBluetooth/IOBluetoothUserLib.h>


/*------------------------------------------------------------------------------
	B l u e t o o t h S e r v i c e
------------------------------------------------------------------------------*/

@interface BluetoothService : NSObject
{
	id delegate;

	IOBluetoothRFCOMMChannel * theChannel;
	IOBluetoothUserNotification * channelOpenNotification;

	// Service Entry for the service we publish
	BluetoothRFCOMMChannelID theChannelId;
	BluetoothSDPServiceRecordHandle serviceHandle;
}

- (void) setDelegate: (id) inDelegate;
- (void) publish: (NSString *) inDictionaryPath;
- (void) stop;

- (NSString *) remoteDeviceName;
- (NSString *) localDeviceName;

- (int) write: (const void *) inData length: (unsigned int) inLength;
- (void) disconnect;

// IOBluetoothRFCOMMChannel delegate methods
- (void) rfcommChannelData: (IOBluetoothRFCOMMChannel *) inChannel data: (void *) inData length: (unsigned int) inLength;
- (void) rfcommChannelClosed: (IOBluetoothRFCOMMChannel *) inChannel;
@end


/*------------------------------------------------------------------------------
	B l u e t o o t h E n d p o i n t
------------------------------------------------------------------------------*/

@interface BluetoothEndpoint : NCEndpoint
{
	BluetoothService * bluetoothService;
	NCChunkBuffer * getQueue;
}
@end

