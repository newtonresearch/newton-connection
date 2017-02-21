 /*
	File:		NCDockProtocolController.mm

	Abstract:	Dock Protocol controller interface.
					The controller knows about the dock protocol.
					It translates requests for Newton data to dock commands
					that it passs to the active session.
					dock
					 session == component
					  dockEventQueue
						endpointController
						 endpoint

	Written by:	Newton Research, 2012.
*/
#import <SystemConfiguration/SCDynamicStoreCopySpecific.h>

#import "NCDockProtocolController.h"
#import "NCDocument.h"
#import "NCWindowController.h"
#import "DockProtocol.h"
#import "PlugInUtilities.h"
#import "NCXPlugIn.h"
#import "NCXErrors.h"
#import "PreferenceKeys.h"
#import "BackupComponent.h"
#import "PackageComponent.h"
#import "PassthruComponent.h"
#import "ScreenshotComponent.h"
#import "ROMComponent.h"
#import "Newton1Component.h"
//#import "SyncComponent.h"
#import "Utilities.h"

extern void		InitDrawing(CGContextRef inContext, int inScreenHeight);
extern void		DrawBits(const char * inBits, unsigned inHeight, unsigned inWidth, unsigned inRowBytes, unsigned inDepth);


// Ids of events we raise
#define kDDoGetAllStores	'GALS'
#define kDDoDeletePkgList	'DPKL'
#define kDDoDeleteIdList	'DIDL'
#define kDDoImport			'IMPT'


/*------------------------------------------------------------------------------
	N C D e s k t o p I n f o

	Record defining the capabilities of the desktop connection app.
------------------------------------------------------------------------------*/

struct NCDesktopInfo
{
	int			protocolVersion;
	int			desktopType;			// 0 for Mac, 1 for Windows
	SNewtNonce	encryptedKey;
	int			sessionType;
	int			allowSelectiveSync;
//	Ref			desktopApps;
};


/*------------------------------------------------------------------------------
	D a t a
------------------------------------------------------------------------------*/

//	definition of this app’s capabilities
const DockAppInfo gNCAppInfo =
{
	"Newton Connection",
	2,	// app id
	1,	// app version
	kSettingUpSession,
	kBackupIcon | kInstallIcon | kImportIcon | kKeyboardIcon,	// | kRestoreIcon | kSyncIcon
	2,	// retry attempts
	0,	// connect delay
};


/*------------------------------------------------------------------------------
	String helper functions.
------------------------------------------------------------------------------*/

NSString *
Stringer(NSArray * inArray)
{
	NSString * str = [NSString stringWithString: [inArray objectAtIndex: 0]];
	int i, count = (int)inArray.count;
	for (i = 1; i < count; i++) {
		str = [str stringByAppendingFormat: @",%@", [inArray objectAtIndex: i]];
	}
	return str;
}


int
FindStringInArray(NSArray * inArray, NSString * inStr)
{
	NSString * str;
	int i, count = (int)inArray.count;
	for (i = 0; i < count; i++) {
		str = [inArray objectAtIndex: i];
		if ([str caseInsensitiveCompare: inStr] == NSOrderedSame)
			return i;
	}
	return -1;
}


/* -----------------------------------------------------------------------------
	N C D o c k P r o t o c o l C o n t r o l l e r
----------------------------------------------------------------------------- */

@interface NCDockProtocolController ()
{
//	dock negotiation info
	RefStruct	newtonName;
	NewtonInfo	newtonInfo;
	SNewtNonce	newtNonce;
	SNewtNonce	deskNonce;
	int			numOfPasswordAttempts;

// state
	BOOL isMerelyChangingEndpointOptions;
	BOOL isAutoDocking;
	BOOL isAutoSyncing;

// file browsing state
	NSURL *		browseFolder;
	NSArray *	browseFilter;

//	package installation
	dispatch_queue_t accessQueue;
	NSMutableArray * pkgQueue;
	unsigned int totalAmount, amountDone;
	BOOL isInstallingToolkitPackage;

// package deletion
	NSArray * pkgList;
	NCStore * pkgStore;

// entry deletion
	NCSoup * soup;
	NSIndexSet * uidList;

// entry import
	NSArray * entriesToImport;
	NSArray * translators;

// ROM dump
	NSURL * romURL;
	NSData * romData;
}
@end

// there is one global NCDockProtocolController
NCDockProtocolController * gNCNub = nil;

@implementation NCDockProtocolController
/* -----------------------------------------------------------------------------
	P r o p e r t i e s
----------------------------------------------------------------------------- */
@synthesize statusText;

/* -----------------------------------------------------------------------------
	Class management of the singleton dock instance.
----------------------------------------------------------------------------- */

+ (BOOL)isAvailable {
	return gNCNub.document == nil;
}


+ (NCDockProtocolController *)bind:(NCDocument *)inDocument {
	if (gNCNub == nil) {
		gNCNub = [[NCDockProtocolController alloc] init];
	}
	if (self.isAvailable) {
		gNCNub.document = inDocument;
		[gNCNub.session open];
		return gNCNub;
	}
	return nil;
}


+ (void)unbind {
	gNCNub.document = nil;
}


#pragma mark Initialization
/* -----------------------------------------------------------------------------
	Initialize a new instance.
----------------------------------------------------------------------------- */
extern Ref * RSgVarFrame;

- (id)init {
	if (self = [super init]) {
		self.document = nil;

		_isTethered = NO;
		isAutoDocking = NO;
		isAutoSyncing = NO;
		isInstallingToolkitPackage = NO;
		self.operationInProgress = kNoActivity;
		browseFolder = nil;
		browseFilter = nil;
		pkgQueue = nil;
		accessQueue = dispatch_queue_create("com.newton.connection.install", NULL);

		srand((unsigned int)clock());
		deskNonce.hi = rand();
		deskNonce.lo = rand();
		memset(&newtonInfo, 0, sizeof(newtonInfo));
		_protocolVersion = 0;
		newtonName = NILREF;

		_session = [[NCSession alloc] init];
		[_session registerEventHandler:self];
		[_session registerEventHandler:[[NCBackupComponent alloc] initWithProtocolController:self]];
		[_session registerEventHandler:[[NCRestoreComponent alloc] initWithProtocolController:self]];
		[_session registerEventHandler:[[NCPackageComponent alloc] initWithProtocolController:self]];
		[_session registerEventHandler:[[NCPassthruComponent alloc] initWithProtocolController:self]];
		[_session registerEventHandler:[[NCScreenshotComponent alloc] initWithProtocolController:self]];
		[_session registerEventHandler:[[NCNewton1Component alloc] initWithProtocolController:self]];
#if defined(forROMDump)
		[_session registerEventHandler:[[NCROMDumpComponent alloc] initWithProtocolController:self]];
#endif

		// start listening for disconnection notifications
		[NSNotificationCenter.defaultCenter addObserver:self
															selector:@selector(disconnected:)
																 name:kDockDidDisconnectNotification
															  object:self];
		// start listening for notifications re: serial port changes
		[NSNotificationCenter.defaultCenter addObserver:self
															selector:@selector(serialPortChanged:)
																name:kSerialPortChanged
															  object:nil];

		isMerelyChangingEndpointOptions = NO;
	}
	return self;
}


- (void)dealloc {
	[NSNotificationCenter.defaultCenter removeObserver:self];
	self.document = nil;
	_session = nil;
}


#pragma mark Connection
/* -----------------------------------------------------------------------------
	Respond to Newton device connection.
	Update the document with info on the Newton.
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (void)connected {
	if (gNCAppInfo.fStartInControl)
		[self setDesktopControl:YES];
	if (gNCAppInfo.fStartStoreSet)
		[self.session setCurrentStore: RA(NILREF) info: NO];

	[self.session startTickler];

	// update the document
	_isTethered = YES;
	dispatch_async(dispatch_get_main_queue(), ^{
		[self.document setDevice:self.newtonName info:self.newtonInfo];

		if (self.protocolVersion >= kDanteProtocolVersion) {
			// show stores
			[self.session doEvent:kDDoGetAllStores];
		} else {	// we’re talking to a Newton 1 ROM
			// user has to preselect the session type in this protocol -- sync / restore / install
			// it’ll be a user default
			NSInteger sessionType = [NSUserDefaults.standardUserDefaults integerForKey:kNewton1SessionType];
			switch (sessionType) {
			case kSynchronizeSession:
			case kRestoreSession:
				[self.document.windowController performSync];	// sets up UI and ends up back in here at -requestSync:
				break;
			case kLoadPackageSession:
				self.statusText = @"Loading package…";
				[self installPackages:@[self.pkgURL]];
				break;
			}
		}
	});
}


/* -----------------------------------------------------------------------------
	Disconnect from Newton device.
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (void)disconnect {
	if (self.isTethered) {
		[self.session sendEvent:kDDisconnect];
	}
	[self.session close];
}


/* -----------------------------------------------------------------------------
	Respond to Newton device disconnection.
	The session’s -waitForEvent posts this notification when its event queue has
	no more events (because its endpoint has an error -- like disconnection).
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (void)disconnected:(NSNotification *)inNotification {
	REPflush();

	if (self.isTethered) {
		_isTethered = NO;
		[NSNotificationCenter.defaultCenter postNotificationName:kDockDidOperationNotification object:self.document userInfo:@{@"operation":[NSNumber numberWithInt:self.operationInProgress],@"error":[NSNumber numberWithInt:kDockErrDisconnected]}];
		self.operationInProgress = kNoActivity;
		[self.document disconnected];
	}
	if (isMerelyChangingEndpointOptions) {
		isMerelyChangingEndpointOptions = NO;
		// we want to listen again after changing serial port (for example)
		[self.session open];
	}
}


/* -----------------------------------------------------------------------------
	Respond to serial port / speed change notification.
	If we are tethered, close the connection; release the session then create a
	new one to start listening again with the new serial parameters.
	Args:		inNotification
	Return:	--
----------------------------------------------------------------------------- */

- (void)serialPortChanged:(NSNotification *)inNotification {
	isMerelyChangingEndpointOptions = YES;
	[self disconnect];
}


#pragma mark Tethered Device Information
/*------------------------------------------------------------------------------
	Return the protocol version negotiated during connection.
	 0 => not connected
	 9 => Newton OS 1
	10 => Newton OS 2 Dante
------------------------------------------------------------------------------*/

//@synthesize protocolVersion;


/*------------------------------------------------------------------------------
	Return the Newton info returned during connection negotiation.
	Args:		--
	Return:	pointer to our ivar
				DO NOT modify this information
------------------------------------------------------------------------------*/

- (const NewtonInfo *)newtonInfo {
	return &newtonInfo;
}


/*------------------------------------------------------------------------------
	Return the name of the Newton owner.
	Args:		--
	Return:	an autoreleased NSString
				nil => there is no Newton device connected
------------------------------------------------------------------------------*/

- (NSString *)newtonName {
	NSString * nameStr = MakeNSString(DeepClone(newtonName));
	if (nameStr != nil && nameStr.length == 0)
		nameStr = NSLocalizedString(@"device name", nil);
	return nameStr;
}


#pragma mark Progress Reporting
/* -----------------------------------------------------------------------------
	Reset progress reporting state.
----------------------------------------------------------------------------- */

- (void)resetProgressFor:(id<NCComponentProtocol>)inHandler {
	[self.document.windowController startProgressFor:inHandler];
}


#pragma mark -
#pragma mark Public Interface
/* -----------------------------------------------------------------------------
	Synchronize Newton Names with OSX Address Book/Contacts.
----------------------------------------------------------------------------- */

- (void)synchronize {
#if 0
	self.operationInProgress = kSyncActivity;
	[self resetProgressFor:[self.session eventHandlerFor:kDSynchronize]];

	if (isAutoSyncing) {
		[self.session doEvent:kDSynchronize];
		isAutoSyncing = NO;
	} else {
		[self.session doEvent:kDDoSynchronize];
	}
#endif
}


/* -----------------------------------------------------------------------------
	Synchronize operation is complete.
----------------------------------------------------------------------------- */

- (void)synchronizeDone:(NewtonErr)inErr {
#if 0
	if (inErr == noErr) {
		[self.session sendEvent:kDOperationDone];	// no reply expected
	}
	self.operationInProgress = kNoActivity;

	dispatch_async(dispatch_get_main_queue(), ^{
		[NSNotificationCenter.defaultCenter postNotificationName:kDockDidOperationNotification object:self.document userInfo:@{@"operation":[NSNumber numberWithInt:kSyncActivity],@"error":[NSNumber numberWithInt:inErr]}];
	});
#endif
}


#pragma mark -
/* -----------------------------------------------------------------------------
	Backup.
	Or, for Newton 1, sync/restore.
----------------------------------------------------------------------------- */

- (void)requestSync {
	if (self.protocolVersion >= kDanteProtocolVersion) {
		self.operationInProgress = kBackupActivity;

		if (isAutoDocking) {
			// post backup request on the event queue to simulate event received
			[self.session doEvent:kDRequestToSync];
		} else {
			[self resetProgressFor:[self.session eventHandlerFor:kDDoRequestToSync]];
			[self.session doEvent:kDDoRequestToSync];
		}

	} else {	// we’re talking to a Newton 1 ROM
		NSInteger sessionType = [NSUserDefaults.standardUserDefaults integerForKey:kNewton1SessionType];
		switch (sessionType) {
		case kSynchronizeSession:
			self.operationInProgress = kBackupActivity;
			[self resetProgressFor:[self.session eventHandlerFor:'BUN1']];
			[self.session doEvent:'BUN1'];
			break;
		case kRestoreSession:
			self.operationInProgress = kRestoreActivity;
			[self resetProgressFor:[self.session eventHandlerFor:'RSN1']];
			[self.session doEvent:'RSN1'];
			break;
		}
	}
}


/* -----------------------------------------------------------------------------
	Back up operation is complete.
----------------------------------------------------------------------------- */

- (void)syncDone:(NewtonErr)inErr {
	if (inErr != kDockErrDisconnected) {
		if (isAutoSyncing)
			[self synchronize];
		else if (self.protocolVersion >= kDanteProtocolVersion)
			[self.session sendEvent:kDOperationDone];	// no reply expected
	}
	self.operationInProgress = kNoActivity;

	dispatch_async(dispatch_get_main_queue(), ^{
		[self.document backedUp:inErr];
		[NSNotificationCenter.defaultCenter postNotificationName:kDockDidOperationNotification object:self.document userInfo:@{@"operation":[NSNumber numberWithInt:kBackupActivity],@"error":[NSNumber numberWithInt:inErr]}];
	});
}


#pragma mark -
/* -----------------------------------------------------------------------------
	Restore Newton from state of this document.
----------------------------------------------------------------------------- */

- (void)requestRestore {
	self.operationInProgress = kRestoreActivity;
	[self resetProgressFor:[self.session eventHandlerFor:kDDoRestore]];

	[self.session doEvent:kDDoRestore];
	// when done release restoreInfo
}


- (void)restoreDone:(NewtonErr)inErr {
	self.document.restoreInfo = nil;

	if (inErr == noErr) {
		if (self.protocolVersion >= kDanteProtocolVersion)
			[self.session sendEvent:kDOperationDone];	// no reply expected
	}
	self.operationInProgress = kNoActivity;

	dispatch_async(dispatch_get_main_queue(), ^{
		[NSNotificationCenter.defaultCenter postNotificationName:kDockDidOperationNotification object:self.document userInfo:@{@"operation":[NSNumber numberWithInt:kRestoreActivity],@"error":[NSNumber numberWithInt:inErr]}];
	});
}


#pragma mark -
/* -----------------------------------------------------------------------------
	Start keyboard passthrough.
----------------------------------------------------------------------------- */

- (void)requestKeyboard:(id)sender {
	self.statusText = @"Connecting keyboard…";
	self.operationInProgress = kKeyboardActivity;
	[self.session doEvent:kDDoRequestKeyboardPassthrough];
	// wait for 'kybd' response
}

// 'kybd' received -- Newton end has activated keyboard passthrough, either acknowledging our request or initiating it remotely
- (void)keyboardActivated {
	self.statusText = nil;
	[self.session suppressTimeout:YES];
	if (self.operationInProgress != kKeyboardActivity) {
		// Newton requests keyboard passthrough
		self.operationInProgress = kKeyboardActivity;
		dispatch_async(dispatch_get_main_queue(), ^{
			[NSNotificationCenter.defaultCenter postNotificationName:kDockDidRequestKeyboardNotification object:self.document];
		});
	}
	// Newton acknowledges keyboard passthrough
	dispatch_async(dispatch_get_main_queue(), ^{
		[NSNotificationCenter.defaultCenter postNotificationName:kDockDidConnectKeyboardNotification object:self.document];
	});
}


/* -----------------------------------------------------------------------------
	Send keyboard text.
	Args:		inText		text to send
				inFlags		command key state
								according to the spec, 1 => cmd key down
								but in testing it appears the flags are actually ignored
	Return:	--
----------------------------------------------------------------------------- */

- (void)sendKeyboardText:(NSString *)inText state:(unsigned short)inFlags {
	if (inText != nil) {
		ArrayIndex strLen;
		if ((strLen = (ArrayIndex)inText.length) > 0) {
			if (strLen == 1) {
				UniChar c[2];
				c[0] = [inText characterAtIndex: 0];
				c[1] = inFlags;
#if defined(hasByteSwapping)
				c[0] = BYTE_SWAP_SHORT(c[0]);
				c[1] = BYTE_SWAP_SHORT(c[1]);
#endif
				[self.session doEvent:kDDoKeyboardChar data:c length:sizeof(c)];
			} else {
				strLen++;	// add nul terminator
				UniChar * str = (UniChar *) malloc(strLen * sizeof(UniChar));
				[inText getCharacters: str];
				str[strLen-1] = 0;
#if defined(hasByteSwapping)
				UniChar * s = str;
				for (ArrayIndex i = strLen; i > 0; i--, s++)
					*s = BYTE_SWAP_SHORT(*s);
#endif
				[self.session doEvent:kDDoKeyboardString data:str length:strLen * sizeof(UniChar)];
				free(str);
			}
		}
	}
}


#pragma mark -
/* -----------------------------------------------------------------------------
	Request screenshot session.
----------------------------------------------------------------------------- */

- (void)requestScreenshot {
	self.statusText = @"Checking Newton device…";
	[self.session doEvent:kDDoRequestScreenCapture];
}

// Newton end has checked whether screenshot resources are available
- (void)screenshotActivated:(NCDockEvent *)inEvent {
	self.statusText = nil;
	if (inEvent.tag == kDResult) {		// => result of Toolkit check
		NewtonErr err = inEvent.value;
		if (err == noErr) {
			[self.session suppressTimeout:YES];
			self.operationInProgress = kScreenshotActivity;
			dispatch_async(dispatch_get_main_queue(), ^{
				[NSNotificationCenter.defaultCenter postNotificationName:kDockDidScreenshotNotification object:self.document];
			});
		} else {
			// Toolkit not present - offer to install it
			dispatch_async(dispatch_get_main_queue(), ^{
				NSAlert * alert = [[NSAlert alloc] init];
				[alert addButtonWithTitle: NSLocalizedString(@"installNTP", nil)];
				[alert addButtonWithTitle: NSLocalizedString(@"cancel", nil)];
				[alert setMessageText: NSLocalizedString(@"no shots", nil)];
				[alert setInformativeText: NSLocalizedString(@"need ntp", nil)];
				[alert setAlertStyle: NSAlertStyleWarning];

				BOOL isInstallRequested = ([alert runModal] == NSAlertFirstButtonReturn);
				if (isInstallRequested) {
					// OK clicked
					//	Install the NTK Toolkit app.
					//	The Toolkit app provides the screen capture function required by our screenshot function.
					NSURL * path = [[NSBundle mainBundle] URLForResource: @"Toolkit" withExtension: @"newtonpkg"];
					isInstallingToolkitPackage = YES;
					[self installPackages:@[path]];
					// when installed, come back here w/ noErr to activate the screenshot panel
				} else {
					// user doesn’t actually want a screenshot
					self.operationInProgress = kNoActivity;
					[NSNotificationCenter.defaultCenter postNotificationName:kDockDidOperationNotification object:self.document userInfo:@{@"operation":[NSNumber numberWithInt:kScreenshotActivity],@"error":[NSNumber numberWithInt:noErr]}];
				}
			});
		}
	}
}

/* -----------------------------------------------------------------------------
	Take screenshot.
	The screenshot function returns a frame:
	{
		top: 0,
		left: 0,
		bottom: 480,
		right: 320,
		rowBytes: 160,
		depth: 4,
		theBits: <binary>
	}
----------------------------------------------------------------------------- */

- (void)takeScreenshot {
	self.statusText = @"Receiving screen image…";
	[self.session doEvent:kDDoTakeScreenshot];
}

// Newton end has captured screenshot
- (void)screenshotReceived:(NCDockEvent *)inEvent {
	self.statusText = nil;
	if (inEvent.tag == 'shot') {	// => inEvent.ref has image frame
		RefArg pixInfo(inEvent.ref);
		if (IsFrame(pixInfo)) {
			int shotHeight = (int)RVALUE(GetFrameSlot(pixInfo, SYMA(bottom))) - (int)RVALUE(GetFrameSlot(pixInfo, SYMA(top)));
			int shotWidth = (int)RVALUE(GetFrameSlot(pixInfo, SYMA(right))) - (int)RVALUE(GetFrameSlot(pixInfo, SYMA(left)));
			NSSize shotSize = NSMakeSize(shotWidth, shotHeight);

		// render bitmap into NSImage
			NSImage * theImage = [[NSImage alloc] initWithSize: shotSize];
			[theImage lockFocus];

			InitDrawing((CGContextRef) [[NSGraphicsContext currentContext] graphicsPort], shotHeight);
			DrawBits(BinaryData(GetFrameSlot(pixInfo, MakeSymbol("theBits"))), shotHeight, shotWidth,
						(int)RINT(GetFrameSlot(pixInfo, MakeSymbol("rowBytes"))), (int)RINT(GetFrameSlot(pixInfo, MakeSymbol("depth"))));

			[theImage unlockFocus];
			self.document.screenshot = theImage;
		}
		// refresh UI
		dispatch_async(dispatch_get_main_queue(), ^{
			[NSNotificationCenter.defaultCenter postNotificationName:kDockDidScreenshotNotification object:self.document];
		});
	}
}


#pragma mark -
/*------------------------------------------------------------------------------
	Install packages.
	Add package info dictionary to a queue:
		path:	NSURL *		URL of package file
		name:	NSString *	package name (extracted from package file)
		size:	unsigned		size of package file
	then create an event to load them onto the Newton device.
	Args:		inFileURLs	array of URLs of package files
	Return:	--
------------------------------------------------------------------------------*/

- (void)installPackages:(NSArray *)inFileURLs {

	self.operationInProgress = kPackageActivity;

	BOOL __block isNewInstallation = NO;
	dispatch_sync(accessQueue, ^{
		NSUInteger i, pkgCount = inFileURLs.count;
		if (pkgCount > 0) {
			if (pkgQueue == nil) {
				pkgQueue = [[NSMutableArray alloc] initWithCapacity: pkgCount];
				totalAmount = amountDone = 0;
				isNewInstallation = YES;
			}

			// calculate total size to be installed
			for (i = 0; i < pkgCount; i++) {
				unsigned int pkSize;
				NSString * pkName;
				NSURL * pkURL = [inFileURLs objectAtIndex: i];
				if (GetPackageDetails(pkURL, &pkName, &pkSize) == noErr)
				{
					totalAmount += pkSize;

					NSDictionary * pkDict = @{ @"URL":pkURL, @"name":pkName, @"size":[NSNumber numberWithUnsignedInt:pkSize] };
					// add pkg path and name to the list to be installed
					[pkgQueue addObject: pkDict];
				}
			}

			if ([pkgQueue count] == 0) {
				pkgQueue = nil;
				isNewInstallation = NO;
			}
		}
	});

	if (isNewInstallation) {
		[self.document.windowController startProgressFor:[self.session eventHandlerFor:kDDoLoadPackageFiles]];
		[self.session doEvent:kDDoLoadPackageFiles];
	}
}


/*------------------------------------------------------------------------------
	Remove and return package info dictionary from the queue.
	Args:		--
	Return:	package info dictionary
				nil => nothing on the queue
------------------------------------------------------------------------------*/
- (void)setTotalPkgSize:(int)inPkgSize {
	totalAmount = inPkgSize;
}

- (int)totalPkgSize {
	return totalAmount;
}

- (NSDictionary *)dequeuePackage {
	NSMutableDictionary *__block pkDict = nil;
	dispatch_sync(accessQueue, ^{
		if (pkgQueue) {
			if ([pkgQueue count] > 0) {
				pkDict = [NSMutableDictionary dictionaryWithDictionary: [pkgQueue objectAtIndex: 0]];
				[pkgQueue removeObjectAtIndex: 0];
				[pkDict setObject: [NSNumber numberWithUnsignedInt: totalAmount] forKey: @"totalSize"];
				// don’t nil the pkgQueue yet -- we want to be able to add to it while pkg is installing
			} else {
				pkgQueue = nil;
			}
		}
	});
	return pkDict;
}


/*------------------------------------------------------------------------------
	Update state when all packages have been dequeued and installed.
	Args:		inErr
	Return:	--
------------------------------------------------------------------------------*/

- (void)installDone:(NewtonErr)inErr {
	self.document.errorStatus = inErr;
	self.operationInProgress = kNoActivity;
	dispatch_async(dispatch_get_main_queue(), ^{
		[self.document.windowController stopProgress];
		if (isInstallingToolkitPackage) {
			isInstallingToolkitPackage = NO;
			// try -screenshotActivated: again, this time w/ noErr
			[self screenshotActivated:[NCDockEvent makeEvent:kDResult value:noErr]];
		} else {
			[NSNotificationCenter.defaultCenter postNotificationName:kDockDidOperationNotification object:self.document userInfo:@{@"operation":[NSNumber numberWithInt:kPackageActivity],@"error":[NSNumber numberWithInt:inErr]}];
		}
	});
}


#pragma mark -
/* -----------------------------------------------------------------------------
	Dump the Newton ROM.
	We install a pext that returns 4K chunks of the ROM and stitch them together
	to create the file.
	Args:		inURL			URL of file in which to save ROM
				sender
	Return:	--
----------------------------------------------------------------------------- */
#if defined(forROMDump)
- (void)dumpROM:(NSURL *)inURL {
	romURL = inURL;
	[self.document.windowController startProgressFor:[self.session eventHandlerFor:kDDoDumpROM]];
	self.operationInProgress = kROMActivity;
	[self.session doEvent:kDDoDumpROM];
}


- (void)dumpROMDone:(NSData *)inData {
	if (inData) {
		NSError *__autoreleasing err = nil;
		[inData writeToURL:romURL options:0 error:&err];
	}
	romURL = nil;
	self.operationInProgress = kNoActivity;
	[self.document.windowController stopProgress];
}
#endif

#pragma mark Cancellation
/* -----------------------------------------------------------------------------
	UI cancel button pressed -- cancel the operation in progress.
	See also -do_opca:
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (void)cancelOperation {
	switch (self.operationInProgress) {
	case kKeyboardActivity:
		[self.session doEvent:kDDoCancelKeyboardPassthrough];
		// kDOpCanceledAck will be ignored
		break;
	case kScreenshotActivity:
		self.statusText = nil;
		[self.session doEvent:kDDoCancelScreenCapture];
		break;
//	case kPackageActivity:			// install event loop (separate thread) will pick up isOperationCancelled
//	case kBackupActivity:			// backup event loop ditto
//	case kRestoreActivity:			// restore event loop ditto
//	case kImportActivity:			// no UI for import cancellation
//	case kSyncActivity:				// sync event loop ditto
	default:
NSLog(@"-[NCDockProtocolController cancelOperation] during activity %d", self.operationInProgress);
		break;
	}
	self.operationInProgress = kNoActivity;
}


#pragma mark Delete soup entries
/*------------------------------------------------------------------------------
	Delete packages.
	If not tethered, do nothing -- packages will be deleted when we next sync.
	Args:		inList		array of NSString* unique package names
	Return:	--
------------------------------------------------------------------------------*/

- (void)deletePackages:(NSArray *)inList onStore:(NCStore *)inStore {
	// we must be tethered
	if (!self.isTethered)
		return;

	pkgList = inList;
	pkgStore = inStore;
	// pass to protocol thread!
	[self.session doEvent:kDDoDeletePkgList];
}


/* -----------------------------------------------------------------------------
	Delete soup entries.
	If not tethered, do nothing -- entries will be deleted when we next sync.
	Args:		inList		array of int _uniqueIds
				inSoup
	Return:	--
----------------------------------------------------------------------------- */

- (void)deleteEntries:(NSIndexSet *)inList from:(NCSoup *)inSoup {
	// we must be tethered
	if (!self.isTethered)
		return;

	soup = inSoup;	// no need to retain
	uidList = inList;
	// pass to protocol thread!
	[self.session doEvent:kDDoDeleteIdList];
}


#pragma mark Import
/* -----------------------------------------------------------------------------
	Import soup entries to Newton.
	We have already added entries to our soup db object, but we need to update
	their _uniqueId and _modTime slots to match Newton.
	Args:		inEntries	array of NCEntry objects
	Return:	--
----------------------------------------------------------------------------- */

- (void)importEntries:(NSArray *)inEntries {
	// we must be tethered
	if (!self.isTethered)
		return;

	self.operationInProgress = kImportActivity;
	entriesToImport = inEntries;
	// pass to protocol thread!
	[self.session doEvent:kDDoImport];
}


- (void)importDone:(NewtonErr)inErr {
	entriesToImport = nil;
	self.operationInProgress = kNoActivity;

	dispatch_async(dispatch_get_main_queue(), ^{
		[NSNotificationCenter.defaultCenter postNotificationName:kDockDidOperationNotification object:self.document userInfo:@{@"operation":[NSNumber numberWithInt:kImportActivity],@"error":[NSNumber numberWithInt:inErr]}];
	});
}


#pragma mark -
#pragma mark Dock Protocol
/* -----------------------------------------------------------------------------
	D o c k   p r o t o c o l   e v e n t   h a n d l i n g
----------------------------------------------------------------------------- */

- (NSArray *)eventTags {
	return [NSArray arrayWithObjects:
							// session negotiation
							@"auto",	// kDRequestToAutoDock
							@"rtdk",	// kDRequestToDock
							@"name",	// kDNewtonName
							@"ninf",	// kDNewtonInfo
							@"pass",	// kDPassword
							// file browsing
							@"rtbr",	// kDRequestToBrowse
							@"dpth",	// kDGetDefaultPath
							@"spth",	// kDSetPath
							@"gfil",	// kDGetFilesAndFolders
							@"gfin",	// kDGetFileInfo
							@"rali",	// kDResolveAlias
							// import
							@"impt",	// kDImportFile
							@"tran",	// kDSetTranslator
							// operation cancellation
							@"opca",	// kDOperationCanceled
//							@"ocaa",	// kDOpCanceledAck
							// miscellaneous
							@"unkn",	// kDUnknownCommand
							@"disc",	// kDDisconnect
							// application
							@"GALS",	// kDDoGetAllStores
							@"DPKL",	// kDDoDeletePkgList
							@"DIDL",	// kDDoDeleteIdList
							@"IMPT",	// kDDoImport
							nil ];
}


- (NSProgress *)setupProgress {
	return nil;
}


/*------------------------------------------------------------------------------
	Tell Newton we’re assuming control of the next transaction.
	Args:		inCmd			YES => assuming control
								NO  => relinquishing control
	Return:	--
------------------------------------------------------------------------------*/

- (void)setDesktopControl:(BOOL)inCmd
{
	if (inCmd) {
		[self.session sendEvent:kDDesktopInControl];
	} else {
		[self.session sendEvent:kDOperationDone];	// Newton doesn’t acknowledge coming out of desktop control
		// stop suppressing endpoint timeout (suppressed by keyboard passthrough and screenshot functions)
		[self.session suppressTimeout:NO];
	}
}


#pragma mark Session Negotiation
/*------------------------------------------------------------------------------
	Newton wants to auto-dock.
	User sets preferences to:
		backup; user may prefer partial backup
		sync
	Newton end disconnects when done.
				kDRequestToAutoDock
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_auto:(NCDockEvent *)inEvent {
	isAutoDocking = YES;
	isAutoSyncing = NO;	// if we allowed sync we might assign [NSUserDefaults.standardUserDefaults boolForKey:kAutoSyncPref];

//	don’t do this now -- we’ll catch the isAutoDocking in doEvent:kDDoGetAllStores -> -do_GALS
//	dispatch_async(dispatch_get_main_queue(), ^{
//		if ([defaults boolForKey:kAutoBackupPref])
//			[self.document.windowController performSync];
//		else if (isAutoSyncing)
//			[self synchronize:nil];
//	});
}


/*------------------------------------------------------------------------------
	Handle kDRequestToDock event.
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_rtdk:(NCDockEvent *)inEvent {
	// read protocol version from request to dock
	// this will be 9 for both OS 1 & OS 2 ROMs, then negotiated upwards later
	_protocolVersion = inEvent.value;
	// respond with session type
	[self.session sendEvent:kDInitiateDocking value: kSettingUpSession];
	// Newton continues negotiation by sending its name
}


/*------------------------------------------------------------------------------
	Handle kDNewtonName event -- the expected response to kDInitiateDocking.
	Event parms:
		NewtonInfo			record describing the Newton device
		UniChar[]			Newton user’s name
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_name:(NCDockEvent *)inEvent {
	int32_t * infoSrc = (int32_t *) inEvent.data;
	unsigned int infoLen = CANONICAL_LONG(*infoSrc);
	infoSrc++;

	// copy info and fix up byte order
	unsigned int readLen = (unsigned int)MIN(infoLen, sizeof(NewtonInfo));
	int32_t * p = (int32_t *) &newtonInfo;
	for (int i = 0; i < readLen/sizeof(int32_t); i++, infoSrc++)
		*p++ = CANONICAL_LONG(*infoSrc);

	// everything else is the name
	infoLen += sizeof(int32_t);	// include length word from now on
	readLen = inEvent.length - infoLen;
	newtonName = AllocateBinary(SYMA(string), readLen);

	// copy name and fix up byte order
	UniChar * nameSrc = (UniChar *) ((char *)inEvent.data + infoLen);
	UniChar * s = (UniChar *) BinaryData(newtonName);
	for (int i = readLen/sizeof(UniChar); i > 0; i--, nameSrc++)
		*s++ = CANONICAL_SHORT(*nameSrc);

	// respond
	if (newtonInfo.fROMVersion < 0x00020000) {
		// this ROM won’t speak the Dante protocol -- fall back
		[self.session sendEvent:kDSetTimeout value: 90];
		[self.session receiveResult];
		// and now the Newton1 session is 'live'
		[self connected];
	} else {
		// assume Newton ROM speaks the Dante protocol -- respond with desktop details

		RefVar appDef(AllocateFrame());
		SetFrameSlot(appDef, SYMA(name), MakeStringFromCString(gNCAppInfo.fAppName));
		SetFrameSlot(appDef, SYMA(id), MAKEINT(gNCAppInfo.fAppID));
		SetFrameSlot(appDef, SYMA(version), MAKEINT(gNCAppInfo.fAppVersion));
		SetFrameSlot(appDef, MakeSymbol("doesAuto"), TRUEREF);		// undocumented, but done by NCU

		RefVar apps(MakeArray(1));
		SetArraySlot(apps, 0, appDef);

		unsigned int appsSize = (unsigned int)FlattenRefSize(apps);
		unsigned int numOfBytes = sizeof(NCDesktopInfo) + appsSize;
		CPtrPipe pipe;

		// Event parms:
		//		NCDesktopInfo	session info
		//		Ref				app info
		NCDesktopInfo * info = (NCDesktopInfo *) malloc(numOfBytes);

		BOOL selectiveSync = YES;
		if (gNCAppInfo.fStartInControl)
			selectiveSync = gNCAppInfo.fStartInControl(gNCAppInfo.fStartStoreSet);

		info->protocolVersion = CANONICAL_LONG(kDanteProtocolVersion);
		info->desktopType = CANONICAL_LONG(kMacDesktop);
		info->encryptedKey.hi = CANONICAL_LONG(deskNonce.hi);
		info->encryptedKey.lo = CANONICAL_LONG(deskNonce.lo);
		info->sessionType = CANONICAL_LONG(gNCAppInfo.fSessionType);
		info->allowSelectiveSync = CANONICAL_LONG(selectiveSync);

		pipe.init(info+1, appsSize, NO, nil);
		FlattenRef(apps, pipe);
		[self.session sendEvent:kDDesktopInfo data: info length: numOfBytes];
		free(info);

		// Newton should continue negotiation by sending its info
	}
}


/*------------------------------------------------------------------------------
	Handle kDNewtonInfo event -- the expected response to kDDesktopInfo.
	This is where the actual protocol version is updated.
	Event parms:
		long			protocol version
		SNewtNonce	challenge key
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void) do_ninf: (NCDockEvent *) inEvent
{
	uint32_t *	src = (uint32_t *) inEvent.data;

	// read actual protocol version
	_protocolVersion = CANONICAL_LONG(*src);
	src++;
	// read challenge key
	newtNonce.hi = CANONICAL_LONG(*src);
	src++;
	newtNonce.lo = CANONICAL_LONG(*src);
	src++;

	// respond with the functions we support
	[self.session sendEvent:kDWhichIcons value: gNCAppInfo.fWhichIcons];
	[self.session receiveResult];

	// disconnect afer one-and-a-half minutes inactivity
	[self.session sendEvent:kDSetTimeout value: 90];

	// Newton should continue negotiation by sending its password
	// we allow 3 attempts (2 retries)
	numOfPasswordAttempts = gNCAppInfo.fPWRetryAttempts;
}


/*------------------------------------------------------------------------------
	Handle kDPassword event -- the expected response to kDSetTimeout.
	A number of password attempts are allowed, so this handler my be called
	several times during the dock procedure.
	After successful password negotiation the session is up.
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void) do_pass: (NCDockEvent *) inEvent
{
	NSString * pass = [NSUserDefaults.standardUserDefaults objectForKey:kPasswordPref];
	const char * str = pass ? [pass UTF8String] : "";
	RefVar passStr(MakeStringFromCString(str));
	SNewtNonce key;
	SNewtNonce password;
	SNewtNonce expected;

	// encrypt our key using our password - this is what we’re expecting
	expected = deskNonce;
	DESCharToKey((UniChar*)BinaryData(passStr), &key);
	DESEncodeNonce(&key, &expected);

	// read the Newton’s password (our key encrypted using its password)
	SNewtNonce * src = (SNewtNonce *) inEvent.data;
	password = *src;
	password.hi = CANONICAL_LONG(password.hi);
	password.lo = CANONICAL_LONG(password.lo);
	if (password.hi == expected.hi
	&&  password.lo == expected.lo) {
		// password is what we were expecting
		// send back the Newton’s key encrypted with our password
		SNewtNonce response = newtNonce;
		DESCharToKey((UniChar*)BinaryData(passStr), &key);
		DESEncodeNonce(&key, &response);

		response.hi = CANONICAL_LONG(response.hi);
		response.lo = CANONICAL_LONG(response.lo);
		[self.session sendEvent:kDPassword data: &response length: sizeof(response)];

		// and now the Newton2 session is 'live'
		[self connected];

	} else {
		// password is wrong
		if (numOfPasswordAttempts-- > 0) {
			// lose a life
			[self.session sendEvent:kDPWWrong];
		} else {
			// game over
			[self.session sendEvent:kDResult value: kDockErrBadPasswordError];
			// this will fail silently -- should we disconnect w/ appropriate error?
		}
	}
}


#pragma mark File Browsing
/*------------------------------------------------------------------------------
	Newton wants to browse for file to install or import, etc.
				kDRequestToBrowse
	Event parms:
		Ref			symbol of type of file to browse
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void) do_rtbr: (NCDockEvent *) inEvent
{
	if (browseFilter)
		browseFilter = nil;

	// data attached to this request specifies type of files to browse: 'packages, 'import, 'syncFiles
	RefVar filetype(inEvent.ref);
	if (EQ(filetype, MakeSymbol("packages"))) {
		browseFilter = @[@"com.newton.package",@"com.apple.installer-package-archive"];
	} else if (EQ(filetype, MakeSymbol("import"))) {
		browseFilter = [[NCXPlugInController sharedController] importFileTypes];
		[self.session sendEvent:kDGetInternalStore expecting: kDInternalStore];
		// ignore the details
	} else if (EQ(filetype, MakeSymbol("syncFiles"))) {
		browseFilter = @[@"com.newton.backup",@"com.newton.device"];
	}
	if (browseFolder == nil) {
	// set up current|default folder
		[self setFolderPath: NSHomeDirectory()];
	}
	[self.session sendEvent:kDResult value:noErr];
}


/*------------------------------------------------------------------------------
	Newton wants the default path.
				kDGetDefaultPath
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void) do_dpth: (NCDockEvent *) inEvent
{
	[self.session sendEvent:kDPath ref:[self buildPath:browseFolder]];
}


/*------------------------------------------------------------------------------
	Set the current file path.
				kDSetPath
	Event parms:
		Ref			array of path elements
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void) do_spth: (NCDockEvent *) inEvent
{
	RefVar pathElements(inEvent.ref);
	NSString * path = @"/";
	FOREACH_WITH_TAG(pathElements, index, element)
		if (RVALUE(index) > 1)		// ignore desktop, root volume name
		{
			if (IsFrame(element))
				element = GetFrameSlot(element, SYMA(name));
			path = [path stringByAppendingPathComponent: MakeNSString(element)];
		}
	END_FOREACH;
	[self setFolderPath: path];
	// respond with files at this path
	[self.session sendEvent:kDFilesAndFolders ref: [self buildFileList]];
}

/*------------------------------------------------------------------------------
	Newton wants a list of the files|folders at the current path.
				kDGetFilesAndFolders
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void) do_gfil: (NCDockEvent *) inEvent
{
	[self.session sendEvent:kDFilesAndFolders ref: [self buildFileList]];
}


/*------------------------------------------------------------------------------
	Newton wants file info.
				kDGetFileInfo
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void) do_gfin: (NCDockEvent *) inEvent
{
	RefVar filename(inEvent.ref);
	[self.session sendEvent:kDFileInfo ref: [self buildFileInfo: MakeNSString(filename)]];
}


/*------------------------------------------------------------------------------
	Newton wants to resolve file alias.
				kDResolveAlias
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void) do_rali: (NCDockEvent *) inEvent
{
	//	We don’t do this.
}


#pragma mark File Browsing Helpers
/*------------------------------------------------------------------------------
	File browsing helpers.
------------------------------------------------------------------------------*/

- (void) setFolderPath: (NSString *) inPath
{
	browseFolder = [NSURL fileURLWithPath: inPath];
}


- (NSURL *) filePath: (RefArg) inFilename
{
	return [browseFolder URLByAppendingPathComponent: MakeNSString(inFilename)];
}


/*------------------------------------------------------------------------------
	Build a NewtonScript array of path components to send to newton.
	Args:		inURL		URL of current folder
	Return:	an array of strings
	To do:	replace root / with volume name
------------------------------------------------------------------------------*/

- (Ref) buildPath: (NSURL *) inURL
{
	RefVar protoElement(AllocateFrame());
	SetFrameSlot(protoElement, SYMA(name), RA(NILREF));
	SetFrameSlot(protoElement, SYMA(type), MAKEINT(kDesktopFolder));

	RefVar pathElement(Clone(protoElement));
	NSString * desktopName = (NSString *) CFBridgingRelease(SCDynamicStoreCopyComputerName(NULL, NULL));

	SetFrameSlot(pathElement, SYMA(name), MakeString(desktopName));
	SetFrameSlot(pathElement, SYMA(type), MAKEINT(kDesktop));

	RefVar path(MakeArray(1));
	SetArraySlot(path, 0, pathElement);

	NSArray * components = [inURL pathComponents];
	for (NSString * componentStr in components)
	{
		pathElement = Clone(protoElement);
		SetFrameSlot(pathElement, SYMA(name), MakeString(componentStr));
		if ([componentStr characterAtIndex: 0] == '/')
			SetFrameSlot(pathElement, SYMA(type), MAKEINT(kDesktopDisk));
		AddArraySlot(path, pathElement);
	}
	return path;
}


/*------------------------------------------------------------------------------
	Build a NewtonScript array of filenames in the current folder.
	Filter the list so that only packages, importable files or sync files
	are visible depending on the type of browsing in progress.
	Args:		--
	Return:	an array of frames describing the files
------------------------------------------------------------------------------*/

- (Ref) buildFileList
{
	RefVar protoFile(AllocateFrame());
	SetFrameSlot(protoFile, SYMA(name), RA(NILREF));
	SetFrameSlot(protoFile, SYMA(type), MAKEINT(kDesktopFolder));

	RefVar list(MakeArray(0));
	NSFileManager * fileManager = NSFileManager.defaultManager;
	NSDirectoryEnumerator * iter = [fileManager enumeratorAtURL:browseFolder
												includingPropertiesForKeys:@[NSURLNameKey, NSURLTypeIdentifierKey, NSURLIsDirectoryKey]
																		 options:NSDirectoryEnumerationSkipsSubdirectoryDescendants + NSDirectoryEnumerationSkipsHiddenFiles
																  errorHandler:NULL];
	for (NSURL * url in iter) {
		int filetype = -1;
		NSString * filename;
		[url getResourceValue:&filename forKey:NSURLNameKey error:NULL];
		NSString * uti;
		[url getResourceValue:&uti forKey:NSURLTypeIdentifierKey error:NULL];
		NSNumber * isFolder;
		[url getResourceValue:&isFolder forKey:NSURLIsDirectoryKey error:NULL];

		if (isFolder.boolValue) {
			filetype = kDesktopFolder;
		} else if (FindStringInArray(browseFilter, uti) >= 0) {
			if ([uti compare:@"com.apple.installer-package-archive"] == NSOrderedSame) {
				// allow .pkg file olny if it really is Newton pkg
				if (IsNewtonPkg(url)) {
					filetype = kDesktopFile;
				}
			} else {
				filetype = kDesktopFile;
			}
		}
		if (filetype >= 0) {
			RefVar fileElement(Clone(protoFile));
			SetFrameSlot(fileElement, SYMA(name), MakeString(filename));
			SetFrameSlot(fileElement, SYMA(type), MAKEINT(filetype));
			AddArraySlot(list, fileElement);
		}
	}
	return list;
}


/*------------------------------------------------------------------------------
	Build a NewtonScript frame of file information to send to newton.
	Args:		inFile		name of file whose info we want
								file is in browseFolder URL
	Return:	a frame
	To do:	return icon as Newton bitmap
------------------------------------------------------------------------------*/

- (Ref) buildFileInfo: (NSString *) inFilename
{
	NSURL * url = [browseFolder URLByAppendingPathComponent:inFilename];
	NSFileManager * fileManager = NSFileManager.defaultManager;
	NSDictionary * attrs = [fileManager attributesOfItemAtPath:url.path error:nil];

	RefVar info(AllocateFrame());

	NSString * str;
	NSError * error;
	if ([url	getResourceValue:&str forKey:NSURLLocalizedTypeDescriptionKey error:&error]) {
		SetFrameSlot(info, SYMA(kind), MakeString(str));
	}

	NSNumber * n;
	if ((n = [attrs objectForKey: NSFileSize])) {
		SetFrameSlot(info, SYMA(size), MAKEINT([n longValue]));
	}

	NSDate * date;
	if ((date = [attrs objectForKey: NSFileCreationDate])) {
		SetFrameSlot(info, MakeSymbol("created"), MakeDate(date));
	}
	if ((date = [attrs objectForKey: NSFileModificationDate])) {
		SetFrameSlot(info, MakeSymbol("modified"), MakeDate(date));
	}

	SetFrameSlot(info, SYMA(path), MakeString([fileManager displayNameAtPath:url.path]));

	SetFrameSlot(info, SYMA(icon), RA(NILREF));

	return info;
}


#pragma mark Import
/*------------------------------------------------------------------------------
	Import file we just browsed.
				kDImportFile
	Args:		inEvent
	Return:	--

	importFormats: {
		Notes: {
			public.plain-text: NoteFromText,
			public.rtf: NoteFromRTF },
		Names: {...},
		...
	}
------------------------------------------------------------------------------*/

- (void) do_impt: (NCDockEvent *) inEvent
{
	RefVar filename(inEvent.ref);
	if (IsFrame(filename))
		filename = GetFrameSlot(filename, SYMA(name));
	browseFilter = nil;
	NSURL * importURL = [browseFolder URLByAppendingPathComponent: MakeNSString(filename)];

	self.operationInProgress = kImportActivity;
	entriesToImport = [NSArray arrayWithObject:importURL];

	NCXPlugInController * plugin = [NCXPlugInController sharedController];
	// use URL extension to find translator
	NSString * utiType = (__bridge NSString *) UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, (__bridge CFStringRef)[importURL pathExtension], NULL);

	translators = [plugin applicationNamesForImport: utiType];
	if ([translators count] == 1)
	{
		[self doImport: 0];
	}
	else
	{
	// build translator list from file type
		int i, count = (int)translators.count;
		RefVar translatorNames(MakeArray(count));
		for (i = 0; i < count; i++)
			SetArraySlot(translatorNames, i, MakeString([translators objectAtIndex: i]));
		[self.session sendEvent:kDTranslatorList ref: translatorNames];
	}
}


/*------------------------------------------------------------------------------
	Set translator for browsed import file.
				kDSetTranslator
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_tran:(NCDockEvent *)inEvent {
	[self doImport: inEvent.value];
}


- (void)doImport:(NSUInteger)index {
	NewtonErr err;
//	[self.document.progress start:@"Importing…"];
	XTRY
	{
		[self.session sendEvent:kDImporting];
		XFAIL(err = [self.session receiveResult])

		NCApp * app = [self.document findApp:translators[index]];
		translators = nil;

		NSURL * url = entriesToImport[0];
		newton_try
		{
			RefVar ref;
			NCXPlugInController * plugin = [NCXPlugInController sharedController];
			[plugin beginImport:app context:self.document source:url];
		//	[self installImportExtensions];	// there are no import extensions
		// stme <- time always done before set soup

			// always import to the default store
			NCStore * store = [self.document defaultStore];
			[self.session setCurrentStore:RA(NILREF) info:NO];
			// the soup (for Dates anyway) depends on an entry’s class
			NCSoup * theSoup = nil;
			while (NOTNIL(ref = [plugin import])) {
				if (theSoup == nil || ![theSoup.name isEqualToString:plugin.importSoupName]) {
					// first time thru, or plugin has imported an entry that belongs to a different soup
					theSoup = [store findSoup:[plugin importSoupName]];
					XFAIL(err = [self.session setCurrentSoup: MakeString(theSoup.name)])
				}
				NCEntry * entryObj;
				[self.session addEntry:ref];
				entryObj = [soup addEntry:ref];
				if (!entryObj) {
					NSLog(@"failed to add imported entry to %@ soup:", plugin.importSoupName);
					PrintObject(ref, 0);
				}
			}
			[plugin endImport];
		}
		newton_catch_all
		{
			NewtonErr err = (NewtonErr)(long)CurrentException()->data;
			NSLog(@"\n#### Exception %s (%d) during import.", CurrentException()->name, err);
		}
		end_try;

		[self.session sendEvent:kDOperationDone];	// no reply expected
	}
	XENDTRY;
	[self importDone:err];
}


#pragma mark Miscellaneous
/*------------------------------------------------------------------------------
	Newton didn’t understand the last event we sent.
				kDUnknownCommand
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_unkn:(NCDockEvent *)inEvent {
	// what do we do now?
}


/*------------------------------------------------------------------------------
	Cancel the current operation.
	This is a request common to many operations, so we hand it off here to
	the currently active component.
				kDOperationCanceled
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_opca:(NCDockEvent *)inEvent {
	[self.session sendEvent:kDOpCanceledAck];
	[self.session suppressTimeout:NO];
	int op = self.operationInProgress;
	self.operationInProgress = kNoActivity;
	dispatch_async(dispatch_get_main_queue(), ^{
		[NSNotificationCenter.defaultCenter postNotificationName:kDockDidCancelNotification object:self.document userInfo:@{ @"operation":[NSNumber numberWithInt:op], @"error":[NSNumber numberWithInt:noErr] }];
	});
}


/*------------------------------------------------------------------------------
	Disconnect.
	Newton wants to disconnect cleanly.
	BTW, spec says this is Desktop -> Newton... Pah!
				kDDisconnect
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_disc:(NCDockEvent *)inEvent {
	// we won’t be receiving any events after this
	[self.session close];
}


#pragma mark Application
/* -----------------------------------------------------------------------------
	Fetch all stores from Newton device.
	By submitting a desktop event we do this in the session’s (background) thread.
	(Should this be wrapped in @try/@catch?)
	NOTE:	DO NOT access the document object from the session thread!
	Event:	kDDoGetAllStores
----------------------------------------------------------------------------- */

- (void)do_GALS:(NCDockEvent *)inEvent {

	self.statusText = @"Fetching stores…";

	newton_try
	{
		NSString * osInfoStr = nil;
		NSDate * manufDate = nil;
		NSString * cpuStr = nil;

		// we may receive autodock event, so we can’t just…
//		RefVar allStores([self.session getAllStores]);

		RefVar allStores;
		NCDockEvent * evt = [self.session sendEvent:kDGetStoreNames expecting: kDAnyEvent];
		if (evt.tag == kDRequestToAutoDock)
		{
			// defer auto event until after we have set up the usual device info
			isAutoDocking = YES;
			evt = [self.session receiveEvent:kDStoreNames];
		}
		if (evt.tag == kDStoreNames)
			allStores = evt.ref;
		else
		{
			allStores = MakeArray(0);
			NSLog(@"expected kDStoreNames, received %@",[evt description]);
		}

		// update persistent stores IN MAIN THREAD
		dispatch_async(dispatch_get_main_queue(), ^{
			FOREACH(allStores, storeRef)
				[self.document addStore:storeRef];
			END_FOREACH;
			[self.document addStore:RA(NILREF)];	// indicate all stores have been added; can build store library
		});

		if (!isAutoDocking)
			[self setDesktopControl:YES];

		// set up device info
		if (self.document.deviceObj.OSinfo == nil)
		{
			// create an OS version string that’s more meaningful than NewtonInfo.fNOSVersion
			RefVar gestalt([self.session getGestalt: 0x01000003]);	//  kGestalt_SystemInfo
			RefVar osVersion(GetFrameSlot(gestalt, MakeSymbol("ROMversionString")));
			RefVar patchVersion(GetFrameSlot(gestalt, MakeSymbol("patchVersion")));
			if (NOTNIL(osVersion) && NOTNIL(patchVersion))
				osInfoStr = [NSString stringWithFormat:@"%@-%lu", MakeNSString(osVersion), RINT(patchVersion)];
			else
				osInfoStr = @"?";

			manufDate = MakeNSDate(GetFrameSlot(gestalt, MakeSymbol("manufactureDate")));

			RefVar cpuType(GetFrameSlot(gestalt, MakeSymbol("CPUtype")));
			RefVar cpuSpeed(GetFrameSlot(gestalt, MakeSymbol("CPUspeed")));
			if (NOTNIL(cpuType)) {
				if (EQRef(cpuType, MakeSymbol("strongARM")))
					cpuStr = @"StrongARM";
				else if (EQRef(cpuType, MakeSymbol("ARM710a")))
					cpuStr = @"ARM 710";
				else if (EQRef(cpuType, MakeSymbol("ARM610a")))
					cpuStr = @"ARM 610";
				else
					cpuStr = [NSString stringWithCString:SymbolName(cpuType) encoding:NSMacOSRomanStringEncoding];
				if (NOTNIL(cpuSpeed))
					cpuStr = [NSString stringWithFormat:@"%.1f MHz  %@", CDouble(cpuSpeed), cpuStr];
			}
		}

		// update persistent device info IN MAIN THREAD
		dispatch_async(dispatch_get_main_queue(), ^{
			if (osInfoStr)
				self.document.deviceObj.OSinfo = osInfoStr;
			if (manufDate)
				self.document.deviceObj.manufactureDate = manufDate;
			if (cpuStr)
				self.document.deviceObj.processor = cpuStr;
		});

		// if preferred, update the Newton date and time
		if ([NSUserDefaults.standardUserDefaults boolForKey:kSetNewtonTimePref]) {
			[self.session setDateTime:MakeDate([NSDate date])];
		}
		[self.session getHexade];

		// while we’re here, we might as well set up the document with some info required for export
		NSFont * devUserFont = nil;
		NSMutableDictionary * devUserFolders = nil;

		RefVar infoRef;
		infoRef = [self.session getUserFont];
		devUserFont = MakeNSFont(infoRef);

		infoRef = [self.session getUserFolders];
		if (IsFrame(infoRef))
		{
			devUserFolders = [NSMutableDictionary dictionaryWithCapacity: Length(infoRef)];
			FOREACH_WITH_TAG(infoRef, folderSym, folderStr)
				// don’t trust the data
				NSString * folderSymStr = [NSString stringWithCString:SymbolName(folderSym) encoding:NSMacOSRomanStringEncoding];
				if (folderSymStr)
					[devUserFolders setObject: [NSString stringWithCharacters: (const unichar *) BinaryData(folderStr) length: Length(folderStr)/sizeof(unichar)-1]
											 forKey: folderSymStr];
			END_FOREACH;
		}

		// update persistent user info IN MAIN THREAD
		dispatch_async(dispatch_get_main_queue(), ^{
			if (devUserFont)
				self.document.userFont = devUserFont;
			if (devUserFolders)
				self.document.userFolders = devUserFolders;
		});
		self.statusText = nil;
	}
	newton_catch_all
	{ }
	end_try;

	dispatch_async(dispatch_get_main_queue(), ^{
		[self.document.windowController populateSourceList];
	});

	if (isAutoDocking) {
		// handle deferred auto event now
		dispatch_async(dispatch_get_main_queue(), ^{
			if ([NSUserDefaults.standardUserDefaults boolForKey:kAutoBackupPref])
				[self.document.windowController performSync];
			else if (isAutoSyncing)
				[self synchronize];
		});
	} else {
		[self setDesktopControl:NO];
	}
}


/* -----------------------------------------------------------------------------
	Delete packages.
	Event:	kDDoDeletePkgList
----------------------------------------------------------------------------- */

- (void) do_DPKL: (NCDockEvent *) inEvent
{
	NewtonErr err = noErr;
	newton_try
	{
		[self setDesktopControl:YES];
		[self.session setCurrentStore:pkgStore.ref info:NO];
		[self.session setCurrentStore:RA(NILREF) info:NO];		// yes, you really need to do this
		for (NSString * pkgName in pkgList)
		{
			[self.session sendEvent:kDRemovePackage ref: MakeString(pkgName)];
			err = [self.session receiveResult];
			// ignore any errors, just keep going
		}
		[self setDesktopControl:NO];
	}
	newton_catch_all
	{
		err = (NewtonErr)(long)CurrentException()->data;
		NSLog(@"\n#### Exception %s (%d) during remove package.", CurrentException()->name, err);
	}
	end_try;

	pkgList = nil;

	self.document.errorStatus = err;
}


/* -----------------------------------------------------------------------------
	Delete soup entries.
	Event:	kDDoDeleteIdList
----------------------------------------------------------------------------- */

- (void) do_DIDL: (NCDockEvent *) inEvent
{
	// build NS array of _uniqueIds
	RefVar idList(MakeArray(0));
	for (NSUInteger idx = [uidList firstIndex]; idx != NSNotFound; idx = [uidList indexGreaterThanIndex:idx])
	{
		AddArraySlot(idList, MAKEINT(idx));
	}
	uidList = nil;

	NewtonErr err = noErr;
	newton_try
	{
		[self setDesktopControl:YES];
		XTRY
		{
			RefVar store(soup.store.ref);
			XFAIL(err = [self.session setCurrentStore: store info: NO])
			XFAIL(err = [self.session setCurrentSoup: MakeString(soup.name)])
			err = [self.session deleteEntryIdList: idList];
		}
		XENDTRY;
		[self setDesktopControl:NO];
	}
	newton_catch_all
	{
		err = (NewtonErr)(long)CurrentException()->data;
		NSLog(@"\n#### Exception %s (%d) during delete.", CurrentException()->name, err);
	}
	end_try;

	self.document.errorStatus = err;
}


/* -----------------------------------------------------------------------------
	Import soup entries.
	Event:	kDDoImport
----------------------------------------------------------------------------- */

- (void) do_IMPT: (NCDockEvent *) inEvent
{
	NewtonErr err;
	newton_try
	{
		[self setDesktopControl:YES];
		XTRY
		{
			NCStore * theStore = nil;
			NCSoup * theSoup = nil;
			for (NCEntry * entry in entriesToImport)
			{
				if (theSoup != entry.soup)
				{
					theSoup = entry.soup;
					if (theStore != theSoup.store)
					{
						theStore = theSoup.store;
						XFAIL(err = [self.session setCurrentStore: theStore.ref info: NO])
					}
					XFAIL(err = [self.session setCurrentSoup: MakeString(theSoup.name)])
				}
				RefVar ref(entry.ref);
				XFAIL(err = [self.session addEntry:ref])
				// session addEntry: updates ref w/ its added _uniqueId and _modTime
				[entry update:ref];
			}
		}
		XENDTRY;
		[self setDesktopControl:NO];
	}
	newton_catch_all
	{
		err = (NewtonErr)(long)CurrentException()->data;
		NSLog(@"\n#### Exception %s (%d) during import.", CurrentException()->name, err);
	}
	end_try;

	[self importDone: err];
}

@end
