/*
	File:		NCDocument.h

	Abstract:	NCX document interface.
					An instance of NCDocument represents a tethered Newton device.
					Its state (stores and their contents) is persistent and updated
					each time the Newton device is synced.

	Written by:	Newton Research, 2012.
*/

#import <SyncServices/SyncServices.h>

#import "NCDocument.h"
#import "NCWindowController.h"
#import "Session.h"
#import "Utilities.h"
#import "NCXErrors.h"
#import "PreferenceKeys.h"
#import "NCXPlugIn.h"
#import "NCSlot.h"
#import "Logging.h"


NSDictionary * gSlotDict;

#define kMinutesSince1904 34714080


/* -----------------------------------------------------------------------------
	Return the id of the Newton device.
	Args:		--
	Return:	an autoreleased NSString
				nil => there is no Newton info
----------------------------------------------------------------------------- */

NSString *
DeviceId(const NewtonInfo * info) {
	NSString * idStr = nil;
	if (info) {
		idStr = [NSString stringWithFormat: @"%04X-%04X-%04X-%04X", info->fSerialNumber[0] >> 16, info->fSerialNumber[0] & 0xFFFF, info->fSerialNumber[1] >> 16, info->fSerialNumber[1] & 0xFFFF];
		if ([idStr isEqualToString:@"0000-0000-0000-0000"]) {
			idStr = [NSString stringWithFormat: @"%u", info->fNewtonID];
		}
	}
	return idStr;
}


#pragma mark -
#pragma mark NSDate extension
@implementation NSDate (NCX)

- (NSDate *) dateWithZeroTime
{
	NSCalendar * calendar = [NSCalendar currentCalendar];
	unsigned unitFlags = NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay | NSCalendarUnitWeekday;
	NSDateComponents * comps = [calendar components:unitFlags fromDate:self];
	[comps setHour:0];
	[comps setMinute:0];
	[comps setSecond:0];
	return [calendar dateFromComponents:comps];
}

@end


#pragma mark -
#pragma mark NCStore
@implementation NCStore(ref)

/* -----------------------------------------------------------------------------
	Create a persistent soup object on a store.
	Args:		inName
				inSignature
				indexes
				info
	Return:	NCSoup instance
----------------------------------------------------------------------------- */

- (NCSoup *) addSoup: (NSString *) inName signature: (int32_t) inSignature indexes: (RefArg) indexes info: (RefArg) info
{
	NSManagedObjectContext * objContext = self.managedObjectContext;
// soup MUST NOT exist on this store
	NCSoup * soup = [NSEntityDescription insertNewObjectForEntityForName: @"Soup"
													 inManagedObjectContext: objContext];
	soup.name = inName;
	soup.lastBackupId = [NSNumber numberWithUnsignedInt: 0];
	soup.lastImportId = [NSNumber numberWithUnsignedInt: kImportIdBase];

	// update all soup info
	soup.signature = [NSNumber numberWithInt:inSignature];
	[soup updateIndex:indexes];
	[soup updateInfo:info];

	// assume there’s no soupDef, or it’s not fully slotted
	soup.descr = @"No soup definition.";
	soup.appName = @"--";

	RefVar soupDef;
	if (NOTNIL(info)
	&&  NOTNIL(soupDef = GetFrameSlot(info, MakeSymbol("soupDef"))))
	{
		RefVar item;
		if (NOTNIL(item = GetFrameSlot(soupDef, SYMA(name))))
			soup.name = MakeNSString(item);

		if (NOTNIL(item = GetFrameSlot(soupDef, MakeSymbol("userDescr"))))
			soup.descr = MakeNSString(item);

		if (NOTNIL(item = GetFrameSlot(soupDef, MakeSymbol("ownerAppName"))))
			soup.appName = MakeNSString(item);
		// if no ownerAppName use ownerApp symbol
		else if (NOTNIL(item = GetFrameSlot(soupDef, MakeSymbol("ownerApp"))))
			soup.appName = [NSString stringWithCString:SymbolName(item) encoding:NSMacOSRomanStringEncoding];
	}

	[self addSoupsObject: soup];
	return soup;
}


/* -----------------------------------------------------------------------------
	Build store frame from properties.
----------------------------------------------------------------------------- */

- (Ref) ref
{
	RefVar storeRef(AllocateFrame());

	// think this is all we need actually
	SetFrameSlot(storeRef, SYMA(name), MakeString(self.name));
	SetFrameSlot(storeRef, SYMA(kind), MakeString(self.kind));
	SetFrameSlot(storeRef, MakeSymbol("signature"), MAKEINT([self.signature intValue]));
	// are these really of any interest?
	SetFrameSlot(storeRef, MakeSymbol("totalSize"), MAKEINT([self.totalSize intValue]));
	SetFrameSlot(storeRef, MakeSymbol("usedSize"), MAKEINT([self.usedSize intValue]));
	SetFrameSlot(storeRef, MakeSymbol("readOnly"), MAKEBOOLEAN(self.isReadOnly));
	SetFrameSlot(storeRef, MakeSymbol("defaultStore"), MAKEBOOLEAN(self.isDefault));
	SetFrameSlot(storeRef, MakeSymbol("storePassword"), MakeString(self.storePassword));
	SetFrameSlot(storeRef, MakeSymbol("storeVersion"), MAKEINT([self.storeVersion intValue]));

	return storeRef;
}

@end


#pragma mark NCSoup
@implementation NCSoup(ref)

- (Ref) infoFrame
{
	RefVar infoRef;
	if (self.info.length > 0)
	{
		CPtrPipe pipe;
		pipe.init((void *)self.info.bytes, self.info.length, NO, nil);
		infoRef = UnflattenRef(pipe);
	}
	if (ISNIL(infoRef))
		infoRef = AllocateFrame();
	return infoRef;
}


- (void) updateInfo: (RefArg) info
{
	CPtrPipe pipe;

	NSUInteger numOfBytes = FlattenRefSize(info);
	void * data = malloc(numOfBytes);
	pipe.init(data, numOfBytes, NO, nil);
	FlattenRef(info, pipe);
	self.info = [NSData dataWithBytesNoCopy: data length: numOfBytes];

	Ref timeRef = NILREF;
	if (NOTNIL(info))
		timeRef = GetFrameSlot(info, MakeSymbol("NCKLastBackupTime"));
	if (ISNIL(timeRef))
		timeRef = MAKEINT(0);
	self.lastBackupTime = [NSNumber numberWithUnsignedLong:RVALUE(timeRef)];
}


- (Ref) indexArray
{
	if (self.indexes.length == 0)
		return MakeArray(0);
	CPtrPipe pipe;
	pipe.init((void *)self.indexes.bytes, self.indexes.length, NO, nil);
	return UnflattenRef(pipe);
}


- (void) updateIndex: (RefArg) index
{
	CPtrPipe pipe;

	NSUInteger numOfBytes = FlattenRefSize(index);
	void * data = malloc(numOfBytes);
	pipe.init(data, numOfBytes, NO, nil);
	FlattenRef(index, pipe);
	self.indexes = [NSData dataWithBytesNoCopy: data length: numOfBytes];
}


- (Ref) entryWithId: (NSUInteger) inId
{
	NSManagedObjectContext * objContext = self.managedObjectContext;
	NSFetchRequest * request = [[NSFetchRequest alloc] init];
	request.entity = [NSEntityDescription entityForName:@"Entry" inManagedObjectContext:objContext];
	request.predicate = [NSPredicate predicateWithFormat:@"soup = %@ AND uniqueId = %d", self, inId];

	NSError *__autoreleasing error = nil;
	NSArray * results = [objContext executeFetchRequest:request error:&error];

	if (results.count > 0) {
		NCEntry * entry = (NCEntry *)results[0];
		return entry.ref;
	}
	return NILREF;
}


/* -----------------------------------------------------------------------------
	Add an entry object to a soup.
	For cases where we have modified the soup entry frame after receiving it
	in a dock event; ie for packages or for imported entries
	Args:		inEntry
	Return:	an NCEntry object, which has been added to this soup.
----------------------------------------------------------------------------- */

- (NCEntry *) addEntry: (RefArg) inEntry
{
	Ref idRef = GetFrameSlot(inEntry, SYMA(_uniqueId));
	if (NOTINT(idRef))
	{
		// this must be an imported frame, and since it does not yet belong to a soup
		// it has no _uniqueId, so create a temporary one
		// this will be updated when we complete the import protocol
		// or sync next with a tethered Newton device

		NSUInteger iid = [self.lastImportId unsignedIntValue];
		++iid;
FULL_LOG {
//REPprintf("\nadding entry id = %u",iid);
}
		self.lastImportId = [NSNumber numberWithUnsignedInteger:iid];
		SetFrameSlot(inEntry, SYMA(_uniqueId), MAKEINT(iid));

		// also create a _modTime
		// might not be in sync with Newton, but will similarly be corrected later
		SetFrameSlot(inEntry, SYMA(_modTime), MakeDate([NSDate date]));
	}

	// build the NSOF data
	unsigned int numOfBytes = (unsigned int)FlattenRefSize(inEntry);
	void * data = malloc(numOfBytes);
	CPtrPipe pipe;
	pipe.init(data, numOfBytes, NO, nil);
	FlattenRef(inEntry, pipe);

	// add it as usual
	NCEntry * entry = [self addEntry:inEntry withNSOFData:data length:numOfBytes];
	// entry has a copy of the data, so free what we alloc’d
	free(data);
	return entry;
}


/* -----------------------------------------------------------------------------
	Add an entry object to a soup.
	This is the designated method for adding a soup entry: where we receive a
	kDEntry event during backup/synch -- so we already have the NSOF data for
	creating an NCEntry object.
	However, we do test whether an entry with the given uniqueId already exists,
	and if so just update it.
	Args:		inEntry
				inData
				inLength
	Return:	the entry, which has been added to this soup.
----------------------------------------------------------------------------- */

- (NCEntry *) addEntry: (RefArg) inEntry withNSOFData: (void *) inData length: (NSUInteger) inLength
{
	NSManagedObjectContext * objContext = self.managedObjectContext;
	NSFetchRequest * request = [[NSFetchRequest alloc] init];
	request.entity = [NSEntityDescription entityForName:@"Entry" inManagedObjectContext:objContext];

	Ref idRef = GetFrameSlot(inEntry, SYMA(_uniqueId));
	NSUInteger idValue = ((unsigned int)idRef) >> kRefTagBits;	// RVALUE() performs signed conversion
	NSNumber * uid = [NSNumber numberWithUnsignedInteger:idValue];

	request.predicate = [NSPredicate predicateWithFormat:@"uniqueId = %@ AND soup = %@", uid, self];

	NSError *__autoreleasing error = nil;
	NSArray * results = [objContext executeFetchRequest:request error:&error];

	NCEntry * entry;
	if (results.count > 0)
		// we have an existing entry
		entry = (NCEntry *)results[0];
	else
		// entry does not exist on this soup
		entry = [NSEntityDescription insertNewObjectForEntityForName: @"Entry"
											  inManagedObjectContext: objContext];

	entry.refData = [NSData dataWithBytes: inData length: inLength];
//PrintObject(inEntry, 0);

	RefVar entryClass(GetFrameSlot(inEntry, SYMA(class)));
	if (IsSymbol(entryClass))
		entry.refClass = [NSString stringWithCString:SymbolName(entryClass) encoding:NSMacOSRomanStringEncoding];
	else if (FrameHasSlot(inEntry, MakeSymbol("pkgRef")))
		entry.refClass = kPackageRefClass;

	// determine slot to display as title for entries in this soup; default is 'title
	NSString * str = @"";
	NSString * titleSlot = nil;
	NSString * titleType = nil;
	NSArray * colDef = [gSlotDict objectForKey: self.name];
	if (colDef)
	{
		NSDictionary * titleDef = [colDef objectAtIndex:0];
		titleSlot = [titleDef objectForKey: @"slot"];
		titleType = [titleDef objectForKey: @"type"];
	}
	// if no title slot defined for this soup, use default 'title
	if (titleSlot == nil)
	{
		titleSlot = @"title";
		titleType = @"string";
		if (ISNIL(GetFrameSlot(inEntry, SYMA(title))) && NOTNIL(GetFrameSlot(inEntry, SYMA(name))))
			titleSlot = @"name";
	}

	// if type is 'name then we’ve got a Names entry
	if ([titleType isEqualToString:@"name"])
	{
		RefVar locSymbol(MakeSymbol("company"));
		// which might actually be a company name (or worksite or owner)
		if (EQRef(ClassOf(inEntry), locSymbol) && NOTNIL(GetFrameSlot(inEntry, locSymbol)))
		{
			titleSlot = @"company";
			titleType = @"string";
		}
		else if (EQRef(ClassOf(inEntry), MakeSymbol("worksite")) && NOTNIL(GetFrameSlot(inEntry, MakeSymbol("place"))))
		{
			titleSlot = @"place";
			titleType = @"string";
		}
	}

	NCSlot * slotObj = [[NCSlot alloc] init];
	RefVar title(GetFrameSlot(inEntry, MakeSymbol([titleSlot UTF8String])));
	if (NOTNIL(title))
	{
		// transform title using titleDef
		SEL transform = NSSelectorFromString([NSString stringWithFormat:@"transform%@:",[titleType capitalizedString]]);
		if ([entry respondsToSelector: transform])
		{
			slotObj.ref = title;
			str = [entry performSelector:transform withObject: slotObj];
		}
		else if (IsString(title))
			str = MakeNSString(title);
	}
	entry.title = str;

	// remember the highest _uniqueId we have, for incremental backup
	if (idValue < kImportIdBase
	&&  [self.lastBackupId compare: uid] == NSOrderedAscending)
		self.lastBackupId = uid;
	entry.uniqueId = uid;

	NSDate * modTime = MakeNSDate(GetFrameSlot(inEntry, SYMA(_modTime)));
	entry.modTime = modTime ? modTime : [NSDate date];

// could add size of each entry using:
//extern "C" Ref	FEntrySize(RefArg inRcvr, RefArg inEntry);

	if (results.count == 0)	// entry is not already in soup
		[self addEntriesObject:entry];
	return entry;
}

@end


#pragma mark NCEntry
@implementation NCEntry(ref)

- (Ref) ref
{
	CPtrPipe pipe;
	pipe.init((void *)self.refData.bytes, self.refData.length, NO, nil);
	return UnflattenRef(pipe);
}


// after the session has added an entry to a tethered newt,
// the entry’s _uniqueId and _modTime slots are updated
// we need to reflect those changes in our db
- (void) update: (RefArg) inAddedEntry
{
	// rebuild the NSOF data
	unsigned int numOfBytes = (unsigned int)FlattenRefSize(inAddedEntry);
	void * data = malloc(numOfBytes);

	CPtrPipe pipe;
	pipe.init(data, numOfBytes, NO, nil);
	FlattenRef(inAddedEntry, pipe);

	// update the entry object
	self.refData = [NSData dataWithBytesNoCopy: data length: numOfBytes];
	// no need to free(data), the NSData object owns it now

	// update our attributes
	Ref idRef = GetFrameSlot(inAddedEntry, SYMA(_uniqueId));
	NSNumber * uid = [NSNumber numberWithUnsignedInteger:RVALUE(idRef)];
	// remember the highest _uniqueId in our db
	if ([self.soup.lastBackupId compare: uid] == NSOrderedAscending)
		self.soup.lastBackupId = uid;

	self.uniqueId = uid;
	self.modTime = MakeNSDate(GetFrameSlot(inAddedEntry, SYMA(_modTime)));
}

@end


#pragma mark -
@implementation NCDocument
/* -----------------------------------------------------------------------------
	Instance vars.
----------------------------------------------------------------------------- */

- (NCXPlugInController *) pluginController {
	return NCXPlugInController.sharedController;
}

#pragma mark -
#pragma mark Initialization
/* -----------------------------------------------------------------------------
	When we create soup entries, we create a title from a slot that varies
	depending on the soup.
	So we initialize a singleton dictionary used for that soup -> title slot
	lookup.
----------------------------------------------------------------------------- */

+ (void)initialize {
	NSString * path = [[NSBundle mainBundle] resourcePath];
	path = [path stringByAppendingPathComponent: @"slot.plist"];
	gSlotDict = [NSDictionary dictionaryWithContentsOfFile: path];
}


/* -----------------------------------------------------------------------------
	Initialize a new instance.
----------------------------------------------------------------------------- */

- (id)initWithType:(NSString *)typeName error:(NSError **)outError {
	if (self = [super initWithType:typeName error:outError]) {
		// set up the persistent object context
		self.objContext = self.managedObjectContext;
		_objEntities = self.objContext.persistentStoreCoordinator.managedObjectModel.entitiesByName;
		[self.objContext setUndoManager:nil];

		// init state
		self.screenshot = nil;
		self.isReadOnly = NO;

		// create a dock session that listens for a Newton device trying to connect
		self.dock = [NCDockProtocolController bind:self];
	}
	return self;
}


/* -----------------------------------------------------------------------------
	Read instance data from (file) URL.
----------------------------------------------------------------------------- */

- (BOOL)configurePersistentStoreCoordinatorForURL:(NSURL *)url
														 ofType:(NSString *)fileType
										 modelConfiguration:(NSString *)configuration
												 storeOptions:(NSDictionary *)storeOptions
														  error:(NSError **)outError
{
// ignore fileType; it’s always NSSQLiteStoreType
// ignore configuration
// ignore storeOptions; assume NSReadOnlyPersistentStoreOption=[NSNumber numberWithBool:YES]

	// set up the persistent object context
	self.objContext = self.managedObjectContext;
	_objEntities = self.objContext.persistentStoreCoordinator.managedObjectModel.entitiesByName;
	[self.objContext setUndoManager: nil];

	objStore = [self.objContext.persistentStoreCoordinator persistentStoreForURL:url];
	[objStore setReadOnly:YES];
/*
	NSDictionary * options = @{ NSReadOnlyPersistentStoreOption:[NSNumber numberWithBool:YES] };
	*outError = nil;
	objStore = [self.objContext.persistentStoreCoordinator addPersistentStoreWithType:NSSQLiteStoreType
																		  configuration:nil
																		  URL:url
																		  options:options
																		  error:outError];
*/
	return YES;
}


- (BOOL)readFromURL:(NSURL *)absoluteURL ofType:(NSString *)typeName error:(NSError **)outError {

	// init state
	self.dock = nil;
	self.screenshot = nil;
	self.isReadOnly = YES;

	// set up the persistent object context
	self.objContext = self.managedObjectContext;
	_objEntities = self.objContext.persistentStoreCoordinator.managedObjectModel.entitiesByName;
	[self.objContext setUndoManager: nil];

//	NSDictionary * options = @{ NSReadOnlyPersistentStoreOption:[NSNumber numberWithBool:NO] };
//	the store is not readonly, since we allow import to untethered device
	NSDictionary * options = @{ };
	if (outError)
		*outError = nil;
	objStore = [self.objContext.persistentStoreCoordinator addPersistentStoreWithType:NSSQLiteStoreType
																		  configuration:nil
																		  URL:absoluteURL
																		  options:options
																		  error:outError];

	// load device from objContext
	NSFetchRequest * request = [[NSFetchRequest alloc] init];
	request.entity = [NSEntityDescription entityForName:@"Device" inManagedObjectContext:self.objContext];

	*outError = nil;
	NSArray * results = [self.objContext executeFetchRequest:request error:outError];

	if (results.count > 0)
		self.deviceObj = results[0];

	if (*outError) {
		NSLog(@"readFromURL error: %@", (*outError).description);
		if ((*outError).userInfo)
			NSLog(@"%@", (*outError).userInfo.description);
		return NO;
	}
//---- ONE-SHOT RE-INITIALISATION ----
	for (NCStore * store in self.deviceObj.stores)
		for (NCSoup * soup in store.soups)
			soup.lastImportId = [NSNumber numberWithUnsignedInt:kImportIdBase];
//----
	return YES;
}


/* -----------------------------------------------------------------------------
	Make the window controller.
	We need to keep a reference to the controller so we can update our window.
----------------------------------------------------------------------------- */

- (void)makeWindowControllers {
	NCWindowController * windowController = [[NSStoryboard storyboardWithName:@"MainWindow" bundle:nil] instantiateInitialController];
	[self addWindowController: windowController];
	self.windowController = windowController;
	[self.windowController connected:self.dock];
}


/* -----------------------------------------------------------------------------
	When the document is closed, disconnect from the Newton device.
----------------------------------------------------------------------------- */

- (void)close
{
	if (self.fileURL != nil && [self.objContext.persistentStoreCoordinator persistentStoreForURL:self.fileURL] != nil) {
		[self savePersistentStore];		// even if we weren’t tethered, it’s possible we imported some soup entries locally
	}
	if (self.dock) {
		[self.windowController disconnected:self.dock];
		[self.dock disconnect];
		self.dock = nil;
		[NCDockProtocolController unbind];
	}
	[super close];
}


/* -----------------------------------------------------------------------------
	Newton device has disconnected.
	Indicate our new state, but leave the window open.
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (void)disconnected
{
	self.isReadOnly = YES;
	if (self.dock) {
		if (self.fileURL != nil && [self.objContext.persistentStoreCoordinator persistentStoreForURL:self.fileURL] != nil) {
			[self savePersistentStore];
		}
		[self.windowController disconnected:self.dock];
		self.dock = nil;
		[NCDockProtocolController unbind];
		self.errorStatus = kDockErrDisconnected;
	}
}


#pragma mark Core Data thread safety
/* -----------------------------------------------------------------------------
	Create new managed object context for thread.
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (void)makeManagedObjectContextForThread
{
	savedObjContext = self.objContext;

	self.objContext = [[NSManagedObjectContext alloc] initWithConcurrencyType:NSPrivateQueueConcurrencyType];
	[self.objContext setUndoManager:nil];
	[self.objContext setPersistentStoreCoordinator: [savedObjContext persistentStoreCoordinator]];

	[NSNotificationCenter.defaultCenter addObserver:self
													  selector:@selector(mergeChanges:)
															name:NSManagedObjectContextDidSaveNotification
														 object:self.objContext];
}


/* -----------------------------------------------------------------------------
	Merge changes into the main context on the main thread.
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (void) mergeChanges: (NSNotification *) inNotification
{
	[savedObjContext performSelectorOnMainThread:@selector(mergeChangesFromContextDidSaveNotification:)
												 withObject:inNotification
											 waitUntilDone:YES];
}


/* -----------------------------------------------------------------------------
	Finish with managed object context for thread.
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (void)disposeManagedObjectContextForThread {
	[NSNotificationCenter.defaultCenter removeObserver:self name:NSManagedObjectContextDidSaveNotification object:nil];
	self.objContext = savedObjContext;
}


#pragma mark Persistent CoreData
/* -----------------------------------------------------------------------------
	Return the filename for this document.
	The filename is built from the device’s unique id, derived from the device’s NewtonInfo struct.
	If we have a non-zero fSerialNumber then use that in the format xxxx-xxxx-xxxx-xxxx (hex)
	else it’s the fNewtonID and purely numeric (decimal).
	To some extent the filename is irrelevant, since the user need never see it.
	Args:		--
	Return:	autoreleased string
----------------------------------------------------------------------------- */

- (NSString *)syncfilename {
	return [self.deviceObj.visibleId stringByAppendingPathExtension:@"newtondevice"];
}


/* -----------------------------------------------------------------------------
	When Newton connects, find prev NCDocument; load its persistent data; reload UI.
	Files (identified by file URLs) can be named anything; we choose device id
	since that’s unique and means something to the determined browser.
	(Sync files are not visible to the normal user.)
	We store metadata with each persistent store that identifies it by name and id
	so we can present a chooser UI if necessary.
	When searching for a matching NCDocument:
		if no prev, obviously do nothing, this is a new one
		if match found, load persistent data
		if no matching prev, list the prevs and offer to clone one
		“”’s Newton device has not connected before. Would you like to copy data from another of your Newton devices?
		Cancel => continue as for new device
		Copy => load persistent data from chosen document
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */
/*
- (void)loadPreviousSyncState {
	NSURL * foundDevice = nil;
	NSString * thisOne = self.deviceObj.visibleId;
	NSFileManager * fileManager = NSFileManager.defaultManager;
	NSDirectoryEnumerator * iter = [fileManager enumeratorAtURL: ApplicationSupportFolder()
												includingPropertiesForKeys: @[NSURLNameKey,NSURLTypeIdentifierKey]
																		 options: NSDirectoryEnumerationSkipsSubdirectoryDescendants + NSDirectoryEnumerationSkipsHiddenFiles
																  errorHandler: NULL];
	docList = [NSMutableArray arrayWithCapacity:4];
	for (NSURL * url in iter) {
		NSString * filename;
		NSString * filetype;
		[url getResourceValue: &filename forKey: NSURLNameKey error: NULL];
		[url getResourceValue: &filetype forKey: NSURLTypeIdentifierKey error: NULL];

		if ([filetype isEqualToString:@"com.newton.device"]) {
			NSError *__autoreleasing error = nil;
			NSDictionary * metadata = [NSPersistentStoreCoordinator metadataForPersistentStoreOfType:NSSQLiteStoreType URL:url error:&error];
			if (error) {
				NSLog(@"metadata error: %@", error.description);
				if (error.userInfo)
					NSLog(@"%@", error.userInfo.description);
			}
			NSMutableDictionary * docInfo = [NSMutableDictionary dictionaryWithDictionary:metadata];
			[docInfo setObject:url forKey:@"URL"];
			[docList addObject:docInfo];
			if ([metadata[@"NewtonId"] isEqualToString:thisOne])
				foundDevice = url;
		}
	}

	if (foundDevice) {
		// sync against previous data
		// want to add/hide mismatched external stores
		[self usePersistentStore:foundDevice];
	} else {
		if ([docList count] > 0) {
			// list the prevs and offer to clone one
			selectedDoc = [NSIndexSet indexSet];
			[NSBundle loadNibNamed: @"DocChooserSheet" owner: self];
			[NSApp beginSheet: docSheet
					 modalForWindow: [windowController window]
					 modalDelegate: self
					 didEndSelector:  @selector(sheetDidEnd:returnCode:contextInfo:)
					 contextInfo: nil];
		} else {
			// this is the first ever connection
			[self usePersistentStore:ApplicationSupportFile(self.syncfilename)];
		}
	}
}


- (void)usePersistentStore:(NSURL *)inURL {
	[self setFileURL:inURL];

	NSDictionary * options = @{ };
	NSError *__autoreleasing error = nil;
	objStore = [self.objContext.persistentStoreCoordinator addPersistentStoreWithType:NSSQLiteStoreType
																		  configuration:nil
																		  URL:inURL
																		  options:options
																		  error:&error];

	NSDictionary * metadata = @{ @"NewtonName":self.deviceObj.name, @"NewtonId":self.deviceObj.visibleId };
	[self.objContext.persistentStoreCoordinator setMetadata:metadata forPersistentStore:objStore];

	// reloadData
	[windowController addApps];
}
*/

- (void)savePersistentStore {
//NSLog(@"-[document savePersistentStore] objContext=%@\n %@", self.objContext, NSThread.callStackSymbols);
	NSDictionary * metadata = @{ @"NewtonName":self.deviceObj.name, @"NewtonId":self.deviceObj.visibleId };
	[self.objContext.persistentStoreCoordinator setMetadata:metadata forPersistentStore:objStore];
	NSError *__autoreleasing error = nil;
	[self.objContext save:&error];
	if (error) {
		NSLog(@"save error: %@", error.description);
		if (error.userInfo)
			NSLog(@"%@", error.userInfo.description);
	}
}


/* -----------------------------------------------------------------------------
	The doc chooser was dismissed. End the sheet.
	Args:		sender
	Return:	--
----------------------------------------------------------------------------- */

- (IBAction)docChosen:(id)sender {
	[NSApp endSheet:docSheet returnCode:((NSControl *)sender).tag];
}


/* -----------------------------------------------------------------------------
	Notification that the doc chooser sheet is done.
	Args:		inSheet			the sheet
				inResult			the OK/Cancel button result
				inContext		unused
	Return:	--
----------------------------------------------------------------------------- */
/*
- (void)sheetDidEnd:(NSWindow *)inSheet returnCode:(int)inResult contextInfo:(void *)inContext {
	if (inResult == NSModalResponseOK) {
		NSUInteger aRow = [selectedDoc firstIndex];
		if (aRow != NSNotFound) {
			NSDictionary * result = [docList objectAtIndex: aRow];
			[self usePersistentStore:[result objectForKey:@"URL"]];
		}
	} else {
		// user decided not to clone previously-connected store
		NSURL * url = ApplicationSupportFile(self.syncfilename);
		// delete file @ URL if it already exists
		NSError *__autoreleasing error = nil;
		[NSFileManager.defaultManager removeItemAtURL:url error:&error];
		[self usePersistentStore:url];
	}

	[inSheet orderOut: self];
}
*/

#pragma mark Newton model
/* -----------------------------------------------------------------------------
	Set state of backup.
	Args:		inErr
	Return:	--
----------------------------------------------------------------------------- */

- (void)backedUp:(NewtonErr)inErr {
	self.deviceObj.backupDate = [NSDate date];
	self.deviceObj.backupError = [NSNumber numberWithInt:inErr == kDockErrDisconnected ? noErr : inErr];
}


- (BOOL)isBackedUp {
	return self.deviceObj.backupError != nil
		 && self.deviceObj.backupError.intValue == noErr;
}


/* -----------------------------------------------------------------------------
	Format the date to say Today or Yesterday as appropriate.
----------------------------------------------------------------------------- */

- (NSString *)backupDate {
	NSDate * theDate = self.deviceObj.backupDate;
//NSLog(@"-[NCDocument backupDate] deviceObj = %@, date = %@",self.deviceObj,self.deviceObj.backupDate.description);
	if (theDate == nil)
		return NSLocalizedString(@"no backup", @"we have no backup");

	NSDate * dateZero = [theDate dateWithZeroTime];
	NSDate * todayZero = [[NSDate date] dateWithZeroTime];
	NSTimeInterval interval = [todayZero timeIntervalSinceDate:dateZero];
	int dayDiff = interval/(60*60*24);

	// Initialize the formatter.
	NSDateFormatter * formatter = [[NSDateFormatter alloc] init];
	NSString * backedUpStr = NSLocalizedString(@"backedup", @"last time we backed up");

	if (-1 <= dayDiff && dayDiff <= 1) {	// yesterday, today or tomorrow(sic!)
		[formatter setDoesRelativeDateFormatting:YES];
		[formatter setDateStyle:NSDateFormatterMediumStyle];
		[formatter setTimeStyle:NSDateFormatterShortStyle];
/*	} else if (dayDiff <= 7)
	{ // < 1 week ago: show weekday
		[formatter setDateFormat:@"EEEE"];
*/	} else { // show date
		[formatter setDateStyle:NSDateFormatterMediumStyle];
		[formatter setTimeStyle:NSDateFormatterShortStyle];
		backedUpStr = NSLocalizedString(@"backedup on", @"last date we backed up");
	}

	return [NSString stringWithFormat:backedUpStr, [formatter stringFromDate:theDate]];
}


+ (NSSet<NSString *> *)keyPathsForValuesAffectingIsBackedUp {
	return [NSSet setWithObject:@"deviceObj.backupError"];
}

+ (NSSet<NSString *> *)keyPathsForValuesAffectingBackupDate {
	return [NSSet setWithObject:@"deviceObj.backupDate"];
}


/* -----------------------------------------------------------------------------
	UI needs to adjust for restricted options with Newton 1 protocol.
	Args:		--
	Return:	YES => we are talking to a Newton 1 ROM.
----------------------------------------------------------------------------- */

- (BOOL)isNewton1 {
	return self.dock.protocolVersion < kDanteProtocolVersion;
}


/* -----------------------------------------------------------------------------
	Set persistent device object for connected device.
	Args:		inName
				info
	Return:	--
----------------------------------------------------------------------------- */

- (void)setDevice:(NSString *)inName info:(const NewtonInfo *)info {
	self.deviceObj = nil;

	// see if we already know about this device
	NSURL * existingURL = nil;
	NSString * reqdId = DeviceId(info);
	NSFileManager * fileManager = NSFileManager.defaultManager;
	NSDirectoryEnumerator * iter = [fileManager enumeratorAtURL: ApplicationSupportFolder()
												includingPropertiesForKeys: @[NSURLNameKey,NSURLTypeIdentifierKey]
																		 options: NSDirectoryEnumerationSkipsSubdirectoryDescendants + NSDirectoryEnumerationSkipsHiddenFiles
																  errorHandler: NULL];
//NSLog(@"-[document setDevice:%@ info:] looking for %@", inName,reqdId);
	for (NSURL * url in iter) {
		NSString * filename;
		NSString * filetype;
		[url getResourceValue:&filename forKey:NSURLNameKey error:NULL];
		[url getResourceValue:&filetype forKey:NSURLTypeIdentifierKey error:NULL];

		if ([filetype isEqualToString:@"com.newton.device"]) {
			NSError *__autoreleasing error = nil;
			NSDictionary * metadata = [NSPersistentStoreCoordinator metadataForPersistentStoreOfType:NSSQLiteStoreType URL:url options:0 error:&error];
			if (error) {
				NSLog(@"metadata error: %@", error.description);
				if (error.userInfo)
					NSLog(@"%@", error.userInfo.description);
			}
			if ([metadata[@"NewtonId"] isEqualToString:reqdId]) {
				existingURL = url;
				break;
			}
		}
	}

	if (existingURL)
		self.fileURL = existingURL;
	else
		self.fileURL = ApplicationSupportFile([reqdId stringByAppendingPathExtension:@"newtondevice"]);

	NSDictionary * options = @{ };
	NSError *__autoreleasing error = nil;
	objStore = [self.objContext.persistentStoreCoordinator addPersistentStoreWithType:NSSQLiteStoreType
																		  configuration:nil
																		  URL:self.fileURL
																		  options:options
																		  error:&error];
	if (existingURL) {
		// load it from self.objContext

		NSFetchRequest * request = [[NSFetchRequest alloc] init];
		request.entity = [NSEntityDescription entityForName:@"Device" inManagedObjectContext:self.objContext];

		NSError *__autoreleasing error = nil;
		NSArray * results = [self.objContext executeFetchRequest:request error:&error];

		if (results.count > 0)
			self.deviceObj = results[0];
	}
//NSLog(@"found deviceObj = %@",self.deviceObj);

	if (self.deviceObj == nil) {
		// create device info object from NewtonInfo received during connection negotiation
		self.deviceObj = [NSEntityDescription insertNewObjectForEntityForName:@"Device" inManagedObjectContext:self.objContext];
		self.deviceObj.user = [NSEntityDescription insertNewObjectForEntityForName:@"UserInfo" inManagedObjectContext:self.objContext];
		self.deviceObj.user.font = nil;
		self.deviceObj.user.folders = nil;
	}

	self.deviceObj.name = inName;
	self.deviceObj.info = [NSData dataWithBytes:info length:sizeof(NewtonInfo)];
	self.deviceObj.tetheredStores = nil;
}


/* -----------------------------------------------------------------------------
	Add a persistent store object.
	Args:		inStoreRef
	Return:	--
----------------------------------------------------------------------------- */

- (NCStore *)addStore:(RefArg)inStoreRef {
	NCStore * store;

	if (ISNIL(inStoreRef)) {
		// all stores have been added
		// now build set of untethered stores (stores in the “library”)
		NSSet * otherStores = [self.deviceObj.stores filteredSetUsingPredicate:[NSPredicate predicateWithFormat:@"NOT SELF IN %@", self.deviceObj.tetheredStores]];
		if (otherStores.count > 0) {
			self.libraryStores = otherStores;
		}
		store = nil;

	} else {
		// first try to load it from the objContext
		store = [self findStore:inStoreRef];

		// if not found, create it
		if (store == nil) {
			store = [NSEntityDescription insertNewObjectForEntityForName: @"Store"
														inManagedObjectContext: self.objContext];
			// and add it to the device context
			[self.deviceObj addStoresObject: store];
		}

		// update everything about the store
		store.name = MakeNSString(GetFrameSlot(inStoreRef, SYMA(name)));
		store.kind = MakeNSString(GetFrameSlot(inStoreRef, SYMA(kind)));
		store.signature = [NSNumber numberWithInteger:RINT(GetFrameSlot(inStoreRef, MakeSymbol("signature")))];
		store.totalSize = [NSNumber numberWithInteger:RINT(GetFrameSlot(inStoreRef, MakeSymbol("totalSize")))];
		store.usedSize = [NSNumber numberWithInteger:RINT(GetFrameSlot(inStoreRef, MakeSymbol("usedSize")))];

		// Newton 1 might have an info slot, but nothing more
		// so any remaining items MUST be optional
		store.readOnly = [NSNumber numberWithBool:NOTNIL(GetFrameSlot(inStoreRef, MakeSymbol("readOnly")))];
		store.defaultStore = [NSNumber numberWithBool:NOTNIL(GetFrameSlot(inStoreRef, MakeSymbol("defaultStore")))];

		RefVar item;
		item = GetFrameSlot(inStoreRef, MakeSymbol("storeVersion"));
		if (NOTNIL(item))
			store.storeVersion = [NSNumber numberWithInteger:RINT(item)];

		item = GetFrameSlot(inStoreRef, MakeSymbol("storePassword"));
		if (NOTNIL(item))
			store.storePassword = MakeNSString(item);

		[self savePersistentStore];	// need to do this now so that we can find the store later

		[self.deviceObj addTetheredStore:store];
	}

	return store;
}


/* -----------------------------------------------------------------------------
	Find existing store on root device.
	Args:		inStoreRef
	Return:	NCStore instance
----------------------------------------------------------------------------- */

- (NCStore *)findStore:(RefArg)inStoreRef {
	NSNumber * reqdSignature = [NSNumber numberWithInteger:RINT(GetFrameSlot(inStoreRef, MakeSymbol("signature")))];

	NSFetchRequest * request = [[NSFetchRequest alloc] init];
	request.entity = [NSEntityDescription entityForName:@"Store" inManagedObjectContext:self.objContext];
	request.predicate = [NSPredicate predicateWithFormat:@"signature = %@", reqdSignature];

	NSError *__autoreleasing error = nil;
	NSArray * results = [self.objContext executeFetchRequest:request error:&error];

	if (error) {
		NSLog(@"-[NCDocument findStore:] error=%@", error.description);
		if (error.userInfo)
			NSLog(@"%@", error.userInfo.description);
	}

	if (results.count > 0)
		return results[0];

	return nil;
}


/* -----------------------------------------------------------------------------
	Find default store on root device.
	If nothing is marked as default, use the internal store.
	Args:		--
	Return:	NCStore instance
----------------------------------------------------------------------------- */

- (NCStore *)defaultStore {
	NCStore * theStore = nil;
	for (NCStore * store in self.deviceObj.tetheredStores) {
		if (store.isDefault) {
			theStore = store;
			break;
		}
		if (store.isInternal) {
			theStore = store;
		}
	}
	return theStore;
}


/* -----------------------------------------------------------------------------
	Return all the stores available for this device; that is, all the stores
	that have EVER been backed up.
	Args:		--
	Return:	NSArray of NCStore instances, Internal first,
				but otherwise sorted alphabetically by name
----------------------------------------------------------------------------- */

- (NSArray *)stores {
	// load stores array
	NSFetchRequest * request = [[NSFetchRequest alloc] init];
	request.entity = [NSEntityDescription entityForName:@"Store" inManagedObjectContext:self.objContext];

	NSSortDescriptor * sorter = [[NSSortDescriptor alloc] initWithKey:@"name" ascending:YES];
	[request setSortDescriptors:[NSArray arrayWithObject:sorter]];

	NSError *__autoreleasing error = nil;
	NSArray * results = [self.objContext executeFetchRequest:request error:&error];

	if (results.count > 1) {
		NSMutableArray * sortedStores = [NSMutableArray arrayWithArray:results];
		NSUInteger i;
		for (i = 0; i < sortedStores.count; i++) {
			NCStore * store = [sortedStores objectAtIndex:i];
			if ([store.name isEqualToString:@"Internal"]) {
				if (i != 0) {
					[sortedStores removeObjectAtIndex:i];
					[sortedStores insertObject:store atIndex:0];
				}
				results = [NSArray arrayWithArray:sortedStores];
				break;
			}
		}
	}

	return results;
}


/* -----------------------------------------------------------------------------
	Make a persistent app object.
	Find existing app - if not there, create it.
	App name must be unique.
	NOTE: apps do not belong to a store; but their member soups do.
	Args:		inName
	Return:	NCApp instance
----------------------------------------------------------------------------- */

- (NCApp *)findApp:(NSString *)inName {
	NSFetchRequest * request = [[NSFetchRequest alloc] init];
	request.entity = [NSEntityDescription entityForName:@"App" inManagedObjectContext:self.objContext];
	request.predicate = [NSPredicate predicateWithFormat:@"name = %@", inName];

	NSError *__autoreleasing error = nil;
	NSArray * results = [self.objContext executeFetchRequest:request error:&error];

	if (results.count > 0)
		return results[0];

	// app does not exist on this store
	NCApp * app = [NSEntityDescription insertNewObjectForEntityForName: @"App"
													inManagedObjectContext: self.objContext];
	app.name = inName;
	return app;
}


/* -----------------------------------------------------------------------------
	Add a soup to an app. There may be same-named soups in an app, but they
	MUST be on different stores.
	Args:		inApp
				inSoup
	Return:	--
----------------------------------------------------------------------------- */

- (void)app:(NCApp *)inApp addSoup:(NCSoup *)inSoup {
	[inApp addSoupsObject: inSoup];
	// update UI sidebar APPS w/ new soup
	[self.windowController refreshStore:inSoup.store app:inApp];		// don’t pass CD items to another thread
}


#pragma mark Restore info

- (void)buildRestoreInfo {
	/* popuplate:
			storePopup with stores on this device: order by Internal first
			backupPopup with stores in CoreData; ignore Internal
			selecting storePopup --> available backups in backupPopup
			if only one backup for store, hide backupPopup and text
			appsList with apps on selected backupPopup; query apps on store but ignore Packages
			pkgsList with packages on selected backupPopup; query Packages soup on store, list entries
		restores array:
		[ { store: NCStore* (name="Internal"),  // tethered store
			 backups: [
				{ store: NCStore* (name="Internal"),  // library store
				  apps: [ { isSelected: YES, name: "Names" (= NCApp*) },..],
				  pkgs: [ { isSelected: NO, title: "Newton Devices" (= NCEntry*) },..] },..] },..]
		NCRestoreInfo ivars:
			NSArray * allStores = array as above
			NSDictionary * store = selected device store
			NSDictionary * restore = selected backup info
		IB bindings:
			device stores --> array controller --> restoreInfo.allStores .store.name
			selected device store --> restoreInfo.store
			backup stores --> array controller --> selected device store .backups .name
			backup stores is hidden if its count == 1
			selected backup store --> restoreInfo.restore
			app table --> array controller --> restoreInfo.restore .apps
			pkg table --> array controller --> restoreInfo.restore .pkgs
	*/
	NSComparator cmpr = ^(id obj1, id obj2) {
		if ([obj1 isEqualToString:@"Internal"])
			return (NSComparisonResult)NSOrderedAscending;
		return [obj1 compare:obj2];
	};
	NSSortDescriptor * sorter = [NSSortDescriptor sortDescriptorWithKey:@"name" ascending:YES comparator:cmpr];
	// get stores on tethered Newton
	NSArray * devStores = [self.deviceObj.tetheredStores sortedArrayUsingDescriptors:[NSArray arrayWithObject:sorter]];
	// build master list
	NSMutableArray * restores = [NSMutableArray arrayWithCapacity:devStores.count];
	// foreach of those tethered stores, create list of available backups
	for (NCStore * devStore in devStores) {
		BOOL isInternal = devStore.isInternal;
		NSMutableArray * backups = [[NSMutableArray alloc] initWithCapacity:1];
		for (NCStore * store in self.stores) {
			// only add stores of the same kind, internal | external
			if (isInternal == store.isInternal)
				[backups addObject: @{ @"store":store, @"apps":[store apps:NO], @"pkgs":[store pkgs] }];
		}
		[restores addObject: @{ @"store":devStore, @"backups":backups }];
	}
	self.restoreInfo = [[NCRestoreInfo alloc] initStoreInfo:restores];
}


#pragma mark User info
/* -----------------------------------------------------------------------------
	Make a font attribute dictionary from a Newton font ref.
	Args:		inStyle
	Return:	font attribute dictionary
----------------------------------------------------------------------------- */

- (NSDictionary *)makeFontAttribute:(RefArg)inStyle {
	NSFont * aFont = MakeNSFont(inStyle);
	if (aFont == nil)
		aFont = self.userFont;
	return @{ NSFontAttributeName:aFont };
}


/* -----------------------------------------------------------------------------
	userFont setter.
	The document holds a Cocoa representation of a Newton font.
----------------------------------------------------------------------------- */
- (void)setUserFontRef:(Ref)inFontRef {
	userFontRef = inFontRef;
}

- (void)setUserFont:(NSFont *)inFont {
	self.deviceObj.user.font = inFont;
}


/* -----------------------------------------------------------------------------
	userFont getter.
	If we haven’t yet connected, look for a userFont in the app preferences.
	Fall back to HelveticaNeue 12pt; looks good in OS X 10.6.
----------------------------------------------------------------------------- */
- (Ref)userFontRef {
	return userFontRef;
}

- (NSFont *)userFont {
	if (self.deviceObj.user.font == nil) {
		NSUserDefaults * defaults = NSUserDefaults.standardUserDefaults;
		NSString * userFontName = [defaults stringForKey: kUserFontName];
		float userFontSize = [defaults floatForKey: kUserFontSize];
		if (userFontName)
			self.deviceObj.user.font = [NSFont fontWithName: userFontName size: userFontSize];
		if (self.deviceObj.user.font == nil)
			self.deviceObj.user.font = [NSFont fontWithName: @"HelveticaNeue" size: 12.0];
	}
	return self.deviceObj.user.font;
}


/* -----------------------------------------------------------------------------
	userFolders setter.
	The document holds a dictionary that maps Newton folder symbol to user-visible
	name.
----------------------------------------------------------------------------- */

- (void)setUserFolders:(NSDictionary *)inFolders {
	self.deviceObj.user.folders = inFolders;
}


/* -----------------------------------------------------------------------------
	userFolders getter.
----------------------------------------------------------------------------- */

- (NSDictionary *)userFolders {
	if (self.deviceObj.user.folders == nil)
		self.deviceObj.user.folders = [[NSDictionary alloc] init];
	return self.deviceObj.user.folders;
}


#pragma mark NSTableViewDelegate
/* -----------------------------------------------------------------------------
	N S T a b l e V i e w D e l e g a t e   m e t h o d s
----------------------------------------------------------------------------- */

/* -----------------------------------------------------------------------------
	Return the number of rows in a table.
	Args:		inTableView
	Return:	number of rows
----------------------------------------------------------------------------- */

- (NSUInteger)numberOfRowsInTableView:(NSTableView *)inTableView {
	return docList.count;
}


/* -----------------------------------------------------------------------------
	Return the object at a particular cell in a table.
	Args:		inTableView
				inColumn
				inRow
	Return:	the object
----------------------------------------------------------------------------- */

- (id)tableView:(NSTableView *)inTableView objectValueForTableColumn:(NSTableColumn *)inColumn row:(int)inRow {
	return [docList[inRow] objectForKey:inColumn.identifier];
}


#pragma mark Status indication

/* -----------------------------------------------------------------------------
	Indicate result of operation.
	We translate the more common error codes to more meaningful text.
	Args:		inErr			error code, Newton or NCX
	Return:	--
----------------------------------------------------------------------------- */

- (void)setErrorStatus:(NewtonErr)inErr {
	NSString * errStr;

	operationError = inErr;
	if (inErr == kNCErrOperationCancelled) {
		errStr = NSLocalizedString(@"cancelled", @"operation was cancelled");
	} else if (inErr == kDockErrDisconnected) {
		errStr = NSLocalizedString(@"disconnected", @"main window disconnection status");
	} else if (inErr == kNCErrUnknownCommand) {
		errStr = NSLocalizedString(@"unknown command", @"Newton received unkown command");
	} else if (inErr == kNCErrException) {
		errStr = self.exceptionStr;
	} else if (inErr != noErr) {
		errStr = [NSString stringWithFormat: NSLocalizedString(@"sorry", @"an error occurred"), inErr];
	} else {
		errStr = nil;
	}

	self.windowController.progressText = errStr;
}

- (NewtonErr)errorStatus {
	return operationError;
}


#pragma mark Menu items
/* -----------------------------------------------------------------------------
	The menu items we are responsible for are:
	File
		New					only allow new document if none already open
		Install Package	choose .pkg files and install them
		Dump Newton ROM	choose file to contain ROM dump, dump it
	Edit
		Copy					copy screenshot (if available)
		Paste					pass through pasteboard text
		Delete				delete soup entries; whole soups?
		Select All			should apply only to soupInfoController.theTableView
	Newton 1
		Back Up				back up device
		Restore				restore from current(?!) document
		Install package…	install one package

	The document, being in the responder chain, validates these menu items
	and must provide their target actions.
----------------------------------------------------------------------------- */

/* -----------------------------------------------------------------------------
	Enable main menu items as per logic above.
	Args:		inItem
	Return:	YES => enable
----------------------------------------------------------------------------- */

- (BOOL)validateMenuItem:(id<NSValidatedUserInterfaceItem>)inItem {

// File menu
	// we can only create one document at a time
	if (inItem.action == @selector(newDocument:))
		return NCDockProtocolController.isAvailable;
	// we can install a package if we’re not doing anything else
	if (inItem.action == @selector(installPackage:))
		return self.dock.isTethered && self.dock.operationInProgress == kNoActivity;
	// we can dump the ROM if we’re not doing anything else
	if (inItem.action == @selector(dumpROM:))
		return self.dock.isTethered && self.dock.operationInProgress == kNoActivity;

// Edit menu
	// we can copy if we have a screenshot image
	if (inItem.action == @selector(copy:))
		return self.screenshot != nil;

	// we can paste if we have text on the pasteboard and keyboard passthrough is active
	if (inItem.action == @selector(paste:)) {
		if (self.dock.isTethered && self.dock.operationInProgress == kKeyboardActivity) {
			NSPasteboard * pasteboard = [NSPasteboard generalPasteboard];
			NSArray * classes = @[NSString.class];
			NSDictionary * options = @{ };
			return [pasteboard canReadObjectForClasses:classes options:options];
		}
		return NO;
	}

	// Delete and Select All are handled by the NCWindowController
	// since those menu items affect the sidebar

// Newton 1 menu
	// we can select Newton 1 session type if not already in a Newton 2 session
	if (inItem.action == @selector(selectNewton1Session:)) {
		// while we’re here, set the state indicator in the menu item
		[(NSMenuItem *)inItem setState: inItem.tag == [NSUserDefaults.standardUserDefaults integerForKey:kNewton1SessionType] ? NSOnState : NSOffState];
		return !self.dock.isTethered;
	}

	return [super validateUserInterfaceItem:inItem];
}


/* -----------------------------------------------------------------------------
	Create a new document.
	We MUST have this method for validateUserInterfaceItem: to work.
	Args:		sender
	Return:	--
----------------------------------------------------------------------------- */

- (IBAction)newDocument:(id)sender {
	[[NSDocumentController sharedDocumentController] newDocument:sender];
}

- (IBAction)openDocument:(id)sender {
	[[NSDocumentController sharedDocumentController] openDocument:sender];
}


/* -----------------------------------------------------------------------------
	Copy screenshot image onto the pasteboard.
	Args:		sender
	Return:	--
----------------------------------------------------------------------------- */

- (IBAction)copy:(id)sender {
	if (self.screenshot != nil) {
		NSPasteboard * pasteboard = NSPasteboard.generalPasteboard;
		[pasteboard clearContents];
		[pasteboard writeObjects:@[self.screenshot]];
	}
}


/* -----------------------------------------------------------------------------
	Paste text to keyboard passthrough.
	Args:		sender
	Return:	--
----------------------------------------------------------------------------- */

- (IBAction)paste:(id)sender {
	NSPasteboard * pasteboard = [NSPasteboard generalPasteboard];
	NSArray * classes = [NSArray arrayWithObject:[NSString class]];
	NSDictionary * options = @{ };
	NSArray * items = [pasteboard readObjectsForClasses:classes options:options];
	if (items && items.count > 0) {
		NSString * text = items[0];
		[self.dock sendKeyboardText:text state:0];
	}
}


/* -----------------------------------------------------------------------------
	Synchronize Names soup with Contacts, Calendar soups with EventKit.
	NOT YET IMPLEMENTED - and probably never will be.
	Args:		sender
	Return:	--
----------------------------------------------------------------------------- */

- (IBAction)synchronize:(id)sender {
	[self.dock synchronize];
}

@end
