/*
	File:		NCDockProtocolController.h

	Abstract:	Dock Protocol controller interface.
					The controller knows about the dock protocol.
					It translates requests for Newton data to dock command events
					that it passes to the active session.

	Written by:	Newton Research, 2012.
*/

#import <Cocoa/Cocoa.h>
#import "Session.h"


// Values for operationInProgress
enum
{
	kNoActivity,
	kKeyboardActivity,
	kScreenshotActivity,
	kPackageActivity,
	kBackupActivity,
	kRestoreActivity,
	kImportActivity,
	kROMActivity,
	kSyncActivity
};


@class NCDocument, NCStore, NCSoup;

@interface NCDockProtocolController : NSObject<NCComponentProtocol>

@property(nonatomic,weak) NCDocument * document;
@property(nonatomic,readonly) NCSession * session;
@property(nonatomic,readonly) int protocolVersion;
@property(nonatomic,readonly) const NewtonInfo * newtonInfo;
@property(nonatomic,readonly) NSString * newtonName;
@property(nonatomic,strong) NSString * statusText;

@property(nonatomic,readonly) BOOL isTethered;
@property(nonatomic,assign)	 int operationInProgress;

// Newton 1 state
@property(nonatomic,strong)	NSURL * pkgURL;


// control of the only channel available
+ (BOOL)isAvailable;
+ (NCDockProtocolController *)bind:(NCDocument *)inDocument;
+ (void)unbind;

// Connection
// create session and components; start listening on all channels
- (void)connected;
- (void)disconnect;
- (void)disconnected:(NSNotification *)inNotification;

// Progress reporting
- (void)resetProgressFor:(id<NCComponentProtocol>)inHandler;

// Protocol
- (void)setDesktopControl:(BOOL)inCmd;

// Backup/Restore
- (void)requestSync;
- (void)syncDone:(NewtonErr)inErr;
- (void)requestRestore;
- (void)restoreDone:(NewtonErr)inErr;

// Synchronize (Names only for now)
- (void)synchronize;
- (void)synchronizeDone:(NewtonErr)inErr;

// Keyboard passthrough
- (void)requestKeyboard:(id)sender;
- (void)keyboardActivated;
- (void)sendKeyboardText:(NSString *)inText state:(unsigned short)inFlags;

// Screenshot
- (void)requestScreenshot;
- (void)screenshotActivated:(NCDockEvent *)inEvent;
- (void)takeScreenshot;
- (void)screenshotReceived:(NCDockEvent *)inEvent;

// Package installation
- (void)installPackages:(NSArray *)inFileURLs;
- (NSDictionary *)dequeuePackage;
@property int totalPkgSize;
- (void)installDone:(NewtonErr)inErr;

// ROM dump
- (void)dumpROM:(NSURL *)inURL;
- (void)dumpROMDone:(NSData *)inData;

// Cancellation
- (void)cancelOperation;

// Soup modification
- (void)deletePackages:(NSArray *)inList onStore:(NCStore *)inStore;
- (void)deleteEntries:(NSIndexSet *)inIdList from:(NCSoup *)inSoup;
- (void)importEntries:(NSArray *)inEntries;
- (void)doImport:(NSUInteger)index;
- (void)importDone:(NewtonErr)inErr;


// Browsing helper functions
- (void)setFolderPath:(NSString *)inPath;
- (NSURL *)filePath:(RefArg)inFilename;

- (Ref)buildPath:(NSURL *)inURL;
- (Ref)buildFileList;
- (Ref)buildFileInfo:(NSString *)inFile;


// NCComponentProtocol
- (NSArray *)eventTags;

@end


// There is one global NCDockProtocolController
extern NCDockProtocolController * gNCNub;


// Dock protocol notifications
#define kDockDidRequestKeyboardNotification @"DockDidRequestKeyboard"
#define kDockDidConnectKeyboardNotification @"DockDidConnectKeyboard"
#define kDockDidScreenshotNotification @"DockDidScreenshot"
#define kDockDidOperationNotification @"DockDidOperation"
#define kDockDidCancelNotification @"DockDidCancel"
#define kDockDidDisconnectNotification @"DockDidDisconnect"

