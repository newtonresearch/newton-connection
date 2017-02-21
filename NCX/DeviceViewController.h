/*
	File:		DeviceViewController.h

	Abstract:	Interface for NCDeviceViewController class.

	Written by:		Newton Research, 2008.
*/

#import "InfoController.h"
#import "NCDevice.h"


/* -----------------------------------------------------------------------------
	N C D e v i c e I n f o C o n t r o l l e r
	Controller for the device info pane.
	Three items of info are overloaded:
		Manufacturer | Manufactured
		ID | Serial Number
		ROM Version | OS Version
----------------------------------------------------------------------------- */

@interface NCDeviceViewController : NCInfoController
{
	NSMutableArray * rStores;
	int manufactureInfoIndex;
	int serialNumberInfoIndex;
	int versionInfoIndex;
}
@property(readonly) NSString * manufactureLabel;
@property(readonly) NSString * manufactureInfo;
@property(readonly) NSString * serialNumberLabel;
@property(readonly) NSString * serialNumberInfo;
@property(readonly) NSString * versionLabel;
@property(readonly) NSString * versionInfo;

@property(assign) BOOL canSync;
@property(assign) BOOL canRestore;

- (IBAction)nextManufactureInfo:(id)sender;
- (IBAction)nextSerialNumberInfo:(id)sender;
- (IBAction)nextVersionInfo:(id)sender;

- (void)enableSync;
- (IBAction)startSync:(id)sender;

- (void)enableRestore;

@end


@interface NCInfoTextField : NSTextField
@end


/* -----------------------------------------------------------------------------
	N C R e s t o r e V i e w C o n t r o l l e r
	Controller for the restore info sheet.
----------------------------------------------------------------------------- */

@interface NCRestoreViewController : NSViewController
@property(strong) NCDocument * document;

- (IBAction)selectAllApps:(id)sender;
- (IBAction)selectAllPkgs:(id)sender;
- (IBAction)restoreSelection:(id)sender;
@end
