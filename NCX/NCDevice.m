/*
	File:		DeviceInfo.m

	Contains:	Newton connection device info model.

	Written by:	Newton Research, 2011.
*/

#import "NCDevice.h"
#import "DockProtocol.h"

#define KByte 1024

extern NSNumberFormatter * gNumberFormatter;

/*--------------------------------------------------------------------------------
	Global gestalt parameters for GestaltSystemInfo.
	[From NewtonGestalt.h]
--------------------------------------------------------------------------------*/

#define kGestalt_Manufacturer_Apple			0x01000000
#define kGestalt_Manufacturer_Sharp 		0x10000100

#define kGestalt_MachineType_MessagePad	0x10001000
#define kGestalt_MachineType_Lindy			0x00726377
#define kGestalt_MachineType_Bic				0x10002000
#define kGestalt_MachineType_Q				0x10003000
#define kGestalt_MachineType_K				0x10004000


@implementation NCDevice

- (NSString *)identifier {
	return @"Device";
}

@dynamic name;
@dynamic info;
@dynamic OSinfo;
@dynamic manufactureDate;
@dynamic processor;
@dynamic user;
@dynamic backupDate;
@dynamic backupTime;
@dynamic backupError;
@dynamic syncMode;
@dynamic stores;
@synthesize tetheredStores;

/* -----------------------------------------------------------------------------
	Generate UI representation.
----------------------------------------------------------------------------- */

- (NSImage *)image {
	return [NSImage imageNamed:@"source-device.png"];
}

- (NSNumber *)is1xData {
	return [NSNumber numberWithBool:(((NewtonInfo *)self.info.bytes)->fRAMSize == 0)];
}


- (NSString *)manufacturer {
	NSString * str;
	switch (((NewtonInfo *)self.info.bytes)->fManufacturer) {
	case kGestalt_Manufacturer_Apple:
		str = @"Apple";
		break;
	case kGestalt_Manufacturer_Sharp:
		str = @"Sharp";
		break;
	default:
		str = @"Unknown";
	}
	return str;
}

- (NSString *)icon {
	NSString * str;
	switch (((NewtonInfo *)self.info.bytes)->fMachineType) {
	case kGestalt_MachineType_Lindy:
		str = @"MP130.tiff";
		break;
	case kGestalt_MachineType_Q:
		str = @"MP2000.tiff";
		break;
	case kGestalt_MachineType_K:
		str = @"eMate.tiff";
		break;
	default:	// kGestalt_MachineType_MessagePad
		str = @"MP100.tiff";
	}
	return [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:str];
}

- (NSString *)machineType {
	NSString * str;
	switch (((NewtonInfo *)self.info.bytes)->fMachineType) {
	case kGestalt_MachineType_Lindy:
		str = @"MessagePad 130";
		break;
	case kGestalt_MachineType_Q:
		str = @"MessagePad 2000";
		break;
	case kGestalt_MachineType_K:
		str = @"eMate";
		break;
	default:	// kGestalt_MachineType_MessagePad
		str = @"MessagePad";
	}
	return str;
}

- (NSString *)newtonId {
	return [NSString stringWithFormat:@"%u", ((NewtonInfo *)self.info.bytes)->fNewtonID];
}

- (NSString *)serialNumber {
	return [NSString stringWithFormat:@"%04X-%04X-%04X-%04X", ((NewtonInfo *)self.info.bytes)->fSerialNumber[0] >> 16, ((NewtonInfo *)self.info.bytes)->fSerialNumber[0] & 0xFFFF, ((NewtonInfo *)self.info.bytes)->fSerialNumber[1] >> 16, ((NewtonInfo *)self.info.bytes)->fSerialNumber[1] & 0xFFFF];
}

- (NSString *)ROMversion {
	return [NSString stringWithFormat:@"%u.%u", ((NewtonInfo *)self.info.bytes)->fROMVersion >> 16, ((NewtonInfo *)self.info.bytes)->fROMVersion & 0xFFFF];
}

- (NSString *)OSversion {
	if (self.OSinfo != nil && self.OSinfo.length > 2)
		return self.OSinfo;
	return [NSString stringWithFormat:@"%u", ((NewtonInfo *)self.info.bytes)->fNOSVersion];
}

//+ (NSSet<NSString *> *)keyPathsForValuesAffectingOSversion {
//	return [NSSet setWithObject:@"OSinfo"];
//}

- (NSString *)RAMsize {
	return [NSString stringWithFormat:@"%@K", [gNumberFormatter stringFromNumber:[NSNumber numberWithInt:((NewtonInfo *)self.info.bytes)->fRAMSize / KByte]]];
}


- (NSString *)visibleName
{
	NSString * nameStr = self.name;
	if (nameStr != nil) {
		nameStr = [NSString stringWithFormat:NSLocalizedString(@"newton name", nil), nameStr];
	}
	return nameStr;
}

- (NSString *)visibleId
{
	NSString * idStr = nil;
	idStr = self.serialNumber;
	if ([idStr isEqualToString:@"0000-0000-0000-0000"]) {
		idStr = self.newtonId;
	}
	return idStr;
}


/* -----------------------------------------------------------------------------
	Extract screen size from NewtonInfo.
----------------------------------------------------------------------------- */

- (NSSize)screenSize {
	NewtonInfo * p = (NewtonInfo *)self.info.bytes;
	return NSMakeSize(p->fScreenWidth, p->fScreenHeight);
}


/* -----------------------------------------------------------------------------
	Maintain list of stores in the tethered device.
----------------------------------------------------------------------------- */

- (void)addTetheredStore:(NCStore *)inStore {
	if (self.tetheredStores == nil) {
		self.tetheredStores = [[NSMutableArray alloc] initWithCapacity:2];
	}
	[self.tetheredStores addObject:inStore];
}

@end
