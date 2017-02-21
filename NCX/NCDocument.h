/*
	File:		NCDocument.h

	Abstract:	NCX document interface.
					An instance of NCDocument represents a tethered Newton device.
					Its state (stores and their contents) is persistent and updated
					each time the Newton device is synced.

	Written by:	Newton Research, 2012.
*/

#import "NCDevice.h"
#import "NCApp.h"
#import "NCEntry.h"
#import "NCRestoreInfo.h"
#import "NCUserInfo.h"
#import "NewtonKit.h"
#import "DockProtocol.h"
#import "NCDockProtocolController.h"

@class NCWindowController, NCXPlugInController;

@interface NCDocument : NSPersistentDocument
{
	// housekeeping
	NSManagedObjectContext * savedObjContext;
	NSPersistentStore * objStore;

	// transient data
	// existing document chooser
	IBOutlet NSWindow * docSheet;
	NSMutableArray * docList;
	NSIndexSet * selectedDoc;

	// Dock session
	RefStruct userFontRef;

	// state of the connection
	NewtonErr operationError;
}

@property(nonatomic,strong)	NCWindowController * windowController;
@property(nonatomic,strong)	NCDockProtocolController * dock;
@property(nonatomic,weak,readonly)	NCXPlugInController * pluginController;	// belongs to the NCXPlugInController

@property(nonatomic,strong)	NSManagedObjectContext * objContext;
@property(nonatomic,strong)	NSDictionary * objEntities;

// the root of persistent data
@property(nonatomic,strong)	NCDevice * deviceObj;
@property(nonatomic,readonly)	NSString * deviceId;
@property(nonatomic,readonly)	NSArray * stores;
@property(nonatomic,strong)	NSSet * libraryStores;

@property(nonatomic,assign)	Ref userFontRef;
@property(nonatomic,strong)	NSFont * userFont;
@property(nonatomic,strong)	NSDictionary * userFolders;

@property(nonatomic,readonly)	NSString * syncfilename;
@property(nonatomic,assign)	BOOL isReadOnly;
@property(nonatomic,assign,readonly)	BOOL isBackedUp;
@property(nonatomic,readonly)	NSString * backupDate;
@property(nonatomic,assign,readonly)	BOOL isNewton1;

@property(nonatomic,strong)	NSImage * screenshot;

@property(nonatomic,strong)	NCRestoreInfo * restoreInfo;

@property(nonatomic,assign)	NewtonErr errorStatus;

// state
@property(nonatomic,strong)	NSString * exceptionStr;


- (void)makeManagedObjectContextForThread;
- (void)disposeManagedObjectContextForThread;

- (void)backedUp:(NewtonErr)inErr;

// build device state when Newton device connects
// there is only ever one device per document -- it is the root of all other data
- (void)setDevice:(NSString *)inName info:(const NewtonInfo *)info;
// add a store to the device
- (NCStore *)addStore:(RefArg)inStoreRef;
//	find existing store on root device
- (NCStore *)findStore:(RefArg)inStoreRef;
- (NCStore *)defaultStore;
//	find existing app on store - if not there, add it
- (NCApp *)findApp:(NSString *)inName;
- (void)app:(NCApp *)inApp addSoup:(NCSoup *)inSoup;

- (void)buildRestoreInfo;

// connection
//- (void) loadPreviousSyncState;
- (IBAction)docChosen:(id)sender;
//- (void) usePersistentStore: (NSURL *) inURL;
- (void)savePersistentStore;

// user info
- (NSDictionary *)makeFontAttribute:(RefArg)inStyle;

// feedback
- (void)disconnected;

// menus
- (IBAction)synchronize:(id)sender;

@end


/* -----------------------------------------------------------------------------
	Build store frame from properties.
	Do it here rather than in NCStore so we can spare that class from knowing
	anything about Refs.
----------------------------------------------------------------------------- */
@interface NCStore(ref)
- (NCSoup *)addSoup:(NSString *)inName signature:(int32_t)inSignature indexes:(RefArg)indexes info:(RefArg)info;
- (Ref)ref;
@end

@interface NCSoup(ref)
@property(nonatomic,readonly) Ref infoFrame;
@property(nonatomic,readonly) Ref indexArray;
- (void)updateInfo:(RefArg)info;
- (void)updateIndex:(RefArg)index;
- (Ref)entryWithId:(NSUInteger)inId;
- (NCEntry *)addEntry:(RefArg)inEntry;
- (NCEntry *)addEntry:(RefArg)inEntry withNSOFData:(void *)inData length:(NSUInteger)inLength;
@end

@interface NCEntry(ref)
- (Ref)ref;
- (void)update:(RefArg)inAddedEntry;
@end
