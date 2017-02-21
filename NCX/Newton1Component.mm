/*
	File:		Newton1Component.mm

	Contains:	Implementation of the Newton OS 1 protocol handlers.

	Written by:	Newton Research, 2012.
*/

#import "Newton1Component.h"
#import "NCDocument.h"
#import "NCDockProtocolController.h"
#import "PlugInUtilities.h"
#import "NCXErrors.h"
#import "Logging.h"

typedef unsigned int ArrayIndex;

@implementation NCNewton1Component
/* -----------------------------------------------------------------------------
	When we create soup entries, we create a dummy app for it to belong to.
	We can simulate Newton2 apps for known soups; so look them up.
----------------------------------------------------------------------------- */

- (NSString *)appForSoup:(NSString *)inSoupName {
	static NSDictionary * appDict = nil;
	if (appDict == nil) {
		NSString * path = [[NSBundle mainBundle] resourcePath];
		path = [path stringByAppendingPathComponent: @"appslot.plist"];
		appDict = [NSDictionary dictionaryWithContentsOfFile: path];
	}

	NSString * soupKey = [inSoupName lowercaseString];
	NSString * appName = [appDict objectForKey:soupKey];
	if (appName == nil)
		appName = inSoupName;
	return appName;
}


/*------------------------------------------------------------------------------
	Return the event command tags handled by this component.
	Args:		--
	Return:	array of strings
------------------------------------------------------------------------------*/

- (NSArray *)eventTags {
	return [NSArray arrayWithObjects:@"BUN1",	// Back Up Newton 1
												@"RSN1",	// ReStore Newton 1
												nil ];
}


#pragma mark -
/* -----------------------------------------------------------------------------
	Set up our document with the current state of the device: its stores.
----------------------------------------------------------------------------- */

- (Ref)setupDevice {
	NCDocument * document = self.dock.document;

	// no device info available from Newton 1 :(

	// fetch store frames
	RefVar allStores([self.dock.session getAllStores]);
	FOREACH(allStores, storeRef)
		[document addStore: storeRef];
	END_FOREACH;

	return allStores;
}


/* -----------------------------------------------------------------------------
	Set up our document with the current state of the device: its stores.
----------------------------------------------------------------------------- */

- (NSIndexSet *)extractIdsFromEvent:(NCDockEvent *)inEvent {
	NSMutableIndexSet * uIds = [NSMutableIndexSet indexSet];
	uint32_t * uIdPtr = (uint32_t *) inEvent.data;
	uint32_t numOfUIds = CANONICAL_LONG(*uIdPtr);
	for (uIdPtr++; numOfUIds > 0; numOfUIds--, uIdPtr++) {
		[uIds addIndex:CANONICAL_LONG(*uIdPtr)];
	}
	return uIds;
}


/* -----------------------------------------------------------------------------
	Newton 1 device is connected.
	Back up (sync).
	At this point we want to be looking at the device info panel.
	The keyboard and screenshot panels are irrelevant for Newton 1.

	get current time
	foreach store
		set current store
		set last sync time
		get patches
		get package ids
		back up packages...
		get soup names
		get inheritance
		foreach soup name
			set current soup
			get soup info
			get soup ids
			get changed ids	this is for sync;
			delete entries
			add entry...
			return entry...
	disconnect
----------------------------------------------------------------------------- */

- (NSProgress *)setupProgress {
	self.progress = [NSProgress progressWithTotalUnitCount:-1];
	self.progress.localizedDescription = @"Preparing to synchronise…";
	return self.progress;
}


- (void)do_BUN1:(NCDockEvent *)inEvent {
	NewtonErr err;
	NCDockEvent * evt;
	NCDocument * document = self.dock.document;

	newton_try
	{
	/* ---- back up stores ---- */
		RefVar allStores([self setupDevice]);
		unsigned storeIndex = 0, lastStore = Length(allStores) - 1;

		// fake -- just to get the current time | this is what the Docking Protocol spec suggests
		NSNumber * lastTime = document.deviceObj.backupTime;
		unsigned int currentTime = [self.dock.session setLastSyncTime:0];

	/* ---- get patches ---- */
		// only required if syncing system information
		self.dock.statusText = @"Setting up…";		// @"Fetching patch data…";
#if 0
		evt = [session sendEvent:kDGetPatches expecting:kDPatches];
		// spec says kDPatches has no data.
		// hmmm… it looks like a package
		NSData * patchData = [NSData dataWithBytesNoCopy:evt.data length:evt.length freeWhenDone:NO];
		NSString * filename = [document.deviceId stringByAppendingString:@"-patch.pkg"];
		NSError *__autoreleasing error = nil;
		[patchData writeToURL:[ApplicationSupportFolder() URLByAppendingPathComponent:filename] options:0 error:&error];
NSLog(@"received kDPatches %@", evt);
#endif

#if 0
	/* ---- get inheritance ---- */
		evt = [session sendEvent:kDGetInheritance expecting:kDInheritance];
		// evt.data is an array of class, superclass pairs
		// in the format
		//		ULong		number of array elements
		//			C string	class			| pair
		//			C string	superclass	|
		//			<repeat>
		// …don’t know what we’d want to do with that
#endif

		//	set up progress indicator
		//	total = patch + (packages + soups)[num of stores]
		ArrayIndex progressIndex = 0, progressTotal = 1;
		FOREACH(allStores, store)
			[self.dock.session setCurrentStore:store info:NO];

			// we are ignoring packages

			// get [[names],[signatures]] of soups on this store
			RefVar allSoups([self.dock.session getAllSoups]);
			if (NOTNIL(allSoups)) {
				RefVar soupNames;
				if (NOTNIL(soupNames = GetArraySlot(allSoups, 0)))
					progressTotal += Length(soupNames);
				SetFrameSlot(store, SYMA(soups), allSoups);
			}
		END_FOREACH
		self.progress.totalUnitCount = progressTotal;

		FOREACH(allStores, store)
FULL_LOG {
	REPprintf("\nBacking up store\n");
	PrintObject(store, 0);
}
			// the store must already exist in our document
			NCStore * storeObj = [document findStore: store];
			NSString * storeObjName = storeObj.name;
			self.progress.localizedDescription = [NSString stringWithFormat:@"Syncing %@", storeObjName];

	/* ---- set current store ---- */
			[self.dock.session setCurrentStore:store info:NO];

	/* ---- set last sync time ---- */
			[self.dock.session setLastSyncTime:lastTime ? (uint32_t)lastTime.unsignedLongValue : 0];
#if 0
	// for performance reasons, we do not back up/restore packages
	// assume the user can reinstall packages separately

	/* ---- get package ids ---- */
			// for sync; we can calc delta of Newton ids and desktop ids, and delete desktop packages to keep in sync w/ Newton
			evt = [session sendEvent:kDGetPackageIDs expecting:kDPackageIdList];
			// evt.data is a sequence of PackageInfo structs
NSLog(@"received kDPackageIdList %@", evt);

	/* ---- back up packages ---- */
			unwind_protect
			{
				// keep sending kDBackupPackages
				// expect kDPackage+package data; kDResult indicates no more packages
				for ( ; ; ) {
					evt = [session sendEvent:kDBackupPackages expecting:kDAnyEvent];
					if (evt.tag == kDPackage) {
						// evt.data => package Jim, but not as we know it
						// Ulong		id
						// ULong		byte length of name
						//	UniChar	name[]	full name
						//	Ref beyond that is tha package frame?
NSLog(@"received kDPackage %@", evt);
					} else {
						if (evt.tag == kDResult)
							err = evt.value;
						break;
					}
				}
			}
			on_unwind
			{
			}
			end_unwind;
#endif

	/* ---- back up soups ---- */

			RefVar allSoups(GetFrameSlot(store, SYMA(soups)));
			// allSoups is [[names],[signatures]]

			//	foreach soup name
				//	set current soup
				//	get soup info
				//	get soup ids		=> all ids in the soup
				//	get changed ids	=> ids of entries that have changed since lastSyncTime
				//	delete entries		for sync; we can calc delta of Newton ids and desktop ids
				//	add entry...		for sync; add new desktop ids
				//	return entry...

			unwind_protect
			{
				ArrayIndex soupi = 0;
				RefVar soupSignatures(GetArraySlot(allSoups, 1));
				RefVar soupNames(GetArraySlot(allSoups, 0));
				FOREACH(soupNames, theName)
					NSString * soupName = MakeNSString(theName);
					self.progress.localizedDescription = [NSString stringWithFormat:@"Syncing %@ %@", storeObjName,soupName];

	/* ---- set current soup ---- */
FULL_LOG {
	REPprintf("\n\n%s", [soupName UTF8String]);
}
					if ([self.dock.session setCurrentSoup:theName] == noErr) {
						// look up soup’s signature from its name
						int soupSignature = (int)RVALUE(GetArraySlot(soupSignatures, soupi));
						soupi++;

						EventType result = 0;
						RefVar soupInfo, soupIndex;
						NCSoup * soupObj = [storeObj findSoup:soupName];
						NewtonTime lastSyncTime = soupObj ? soupObj.lastSyncTime : 0;
						NewtonTime syncTime = [self.dock.session setLastSyncTime:lastSyncTime];
						XTRY
						{
							evt = [self.dock.session sendEvent:kDGetSoupInfo expecting:kDAnyEvent];
							XFAILIF(evt.tag == kDOperationCanceled, result = kDOperationCanceled;)	// user cancelled
							if (evt.tag == kDSoupInfo)
								soupInfo = evt.ref;

							evt = [self.dock.session sendEvent:kDGetIndexDescription expecting:kDAnyEvent];
							XFAILIF(evt.tag == kDOperationCanceled, result = kDOperationCanceled;)	// user cancelled
							if (evt.tag == kDIndexDescription)
								soupIndex = evt.ref;

							if (soupObj) {
								// soup exists -- update it
								if (NOTNIL(soupInfo))
									[soupObj updateInfo:soupInfo];
								if (NOTNIL(soupIndex))
									[soupObj updateIndex:soupIndex];
								soupObj.signature = [NSNumber numberWithInt:soupSignature];
							} else {
								// create it
								// there is no concept of ‘apps’ in Newton 1
								// so to fit our Newton 2 model, we must create a dummy app for each soup
								NCApp * appObj = [document findApp: [self appForSoup:soupName]];	//	will create app if necessary
								soupObj = [storeObj addSoup:soupName signature:soupSignature indexes:soupIndex info:soupInfo];
								[document app:appObj addSoup:soupObj];
							}

	/* ---- get ids of all entries in soup ---- */
							evt = [self.dock.session sendEvent:kDGetSoupIDs expecting:kDSoupIDs];
							NSIndexSet * allIds = [self extractIdsFromEvent:evt];
							//later we should cropTo: these
							NSIndexSet * fetchIds;
							if (lastSyncTime != 0) {
	/* ---- get ids of changed entries in soup ---- */
								evt = [self.dock.session sendEvent:kDGetChangedIDs expecting:kDChangedIDs];
								fetchIds = [self extractIdsFromEvent:evt];
							}
							else
								fetchIds = allIds;
	/* ---- fetch those entries ---- */
							for (NSUInteger idx = [fetchIds firstIndex]; idx != NSNotFound; idx = [fetchIds indexGreaterThanIndex:idx]) {
								[self.dock.session sendEvent:kDReturnEntry value:(int)idx];
								evt = [self.dock.session receiveEvent:kDAnyEvent];
								// expecting kDEntry, kDOperationCanceled
								if (evt.tag == kDEntry) {
									[soupObj addEntry:evt.ref withNSOFData:evt.data length:evt.dataLength];
								} else if (evt.tag == kDOperationCanceled) {
									result = evt.tag;
									break;
								}
							}

							if (lastSyncTime != 0 && result != kDOperationCanceled) {
	/* ---- delete entries locally that were deleted from Newton ---- */
								[soupObj cropTo:allIds];

	/* ---- delete entries that were deleted locally ---- */
								NSIndexSet * localIds = soupObj.currIds;
								RefVar didList(MakeArray(0));
								for (NSUInteger idx = [allIds firstIndex]; idx != NSNotFound; idx = [allIds indexGreaterThanIndex:idx]) {
									if (![localIds containsIndex:idx])
										AddArraySlot(didList, MAKEINT(idx));
								}
								if (Length(didList) > 0)
									[self.dock.session deleteEntryIdList:didList];

/* ---- send entries that were imported locally ---- */
//      actually, we can’t import to Newton1 (yet)
								if (storeObj.isDefault || storeIndex == lastStore) {
									NSArray * importedEntries = [soupObj importedEntries];
									if (importedEntries && importedEntries.count > 0) {
										for (NCEntry * entry in importedEntries) {
											RefVar ref(entry.ref);
											XFAIL(err = [self.dock.session addEntry:ref])
											// session addEntry: updates ref w/ its added _uniqueId and _modTime
											[entry update:ref];
										}
									}
								}
							}
						}
						XENDTRY;

						// good time to save the document?
						[document savePersistentStore];

						// acknowledge cancellation from Newton end and cancel this loop
						if (result == kDOperationCanceled) {
							[self.dock.session sendEvent:kDOpCanceledAck];
							ThrowErr(exStore, kNCErrOperationCancelled);

						// only attempt to cancel backup after entire soup has been sent
						} else if (self.progress.isCancelled) {
							ThrowErr(exStore, kNCErrOperationCancelled);

						} else {
							// success
							if (ISNIL(soupInfo))
								soupInfo = soupObj.infoFrame;
							SetFrameSlot(soupInfo, MakeSymbol("NCKLastBackupTime"), MAKEINT(syncTime));
							[soupObj updateInfo:soupInfo];
							[self.dock.session setSoupInfo:soupInfo];
						}
					}
else REPprintf("\n#### soup not on store!");

				self.progress.completedUnitCount = ++progressIndex;
				END_FOREACH	// soup

			// at this point we have a full backup on this store
			}
			on_unwind
			{
			}
			end_unwind;

		storeIndex++;
		END_FOREACH	// store

		// at this point we have a successful backup
		document.deviceObj.backupTime = [NSNumber numberWithUnsignedLong: currentTime];
	}
	newton_catch_all
	{
		err = (NewtonErr)(long)CurrentException()->data;
		REPprintf("\n#### Exception %s (%d) while backing up.\n", CurrentException()->name, err);
	}
	end_try;

	[self.dock syncDone:err];
	[self.dock disconnect];
}


/* -----------------------------------------------------------------------------
	Newton 1 device is connected.
	Restore.
	foreach store
		set current store
		foreach soup
			empty soup
			add entry...
		delete all packages
		delete package directory
		load package...
	disconnect
----------------------------------------------------------------------------- */

- (void)do_RSN1:(NCDockEvent *)inEvent
{
	NewtonErr err;
	NCDockEvent * evt;
	NCDocument * document = self.dock.document;

	RefVar allStores([self setupDevice]);

	/* ---- restore stores ---- */
	newton_try
	{
		BOOL newtonCancelled = NO;

		//	set up progress indicator
		//	total = patch + (packages + soups)[num of stores]
		ArrayIndex progressIndex = 0, progressTotal = 1;
		FOREACH(allStores, store)
			NCStore * storeObj = [document findStore: store];

			// plus one for packages -- all of ’em
			progressTotal++;

			progressTotal += storeObj.soups.count;
		END_FOREACH
		self.progress.totalUnitCount = progressTotal;

		FOREACH(allStores, store)
FULL_LOG {
	REPprintf("\nRestoring store\n");
	PrintObject(store, 0);
}
			// the store must already exist in our document
			NCStore * storeObj = [document findStore: store];
// we should check that this store actually contains data to be restored
			NSString * storeObjName = storeObj.name;

	/* ---- set current store ---- */
			[self.dock.session setCurrentStore:store info:NO];

	/* ---- iterate over soups on store ---- */
			for (NCSoup * soupObj in storeObj.soups) {
				if (newtonCancelled)
					break;

				NSString * soupObjName = soupObj.name;
				self.progress.localizedDescription = [NSString stringWithFormat:@"Restoring %@ %@", storeObjName,soupObjName];

				// if the soup already exists, delete it
				RefVar soupName(MakeString(soupObj.name));
				if ([self.dock.session setCurrentSoup:soupName] == noErr)
					[self.dock.session deleteSoup];

	/* ---- recreate soup ---- */
				[self.dock.session createSoup:soupName index:soupObj.indexArray];
				[self.dock.session setCurrentSoup:soupName];
	/* ---- set its info ---- */
				[self.dock.session setSoupInfo:soupObj.infoFrame];

	/* ---- recreate all soup entries ---- */
				for (NCEntry * entry in [soupObj orderedEntries]) {
					[self.dock.session sendEvent:kDAddEntry data:entry.refData.bytes length:(unsigned int)entry.refData.length];
					evt = [self.dock.session receiveEvent:kDAnyEvent];	// kDResult | kDOperationCanceled
					if (evt.tag == kDResult && evt.value != noErr) {
						err = evt.value;
						REPprintf("\n#### Error %d restoring soup entry.\n", err);
						break;
					}
					if (evt.tag == kDOperationCanceled) {
						[self.dock.session sendEvent:kDOpCanceledAck];
						newtonCancelled = YES;
						break;
					}
				}	// foreach entry
				self.progress.completedUnitCount = ++progressIndex;
			}	// foreach soup
			// don’t cancel partial soup
			if (self.progress.isCancelled || newtonCancelled)
				ThrowErr(exStore, kNCErrOperationCancelled);
#if 0
	// for performance reasons, we do not back up/restore packages
	// assume the user can reinstall packages separately

	/* ---- delete all packages on this store ---- */
			[session sendEvent:kDDeleteAllPackages];
			err = [session receiveResult];
			// do something with the result

	/* ---- delete package directory on this store ---- */
			[session sendEvent:kDDeletePkgDir];
			err = [session receiveResult];
			// do something with the result

	/* ---- load packages from our db ---- */
			for (NCEntry * pkg in [storeObj pkgs])self.{
				NSString * pkgName = pkg.title;
				progress.localizedDescription = [NSString stringWithFormat: NSLocalizedString(@"restoring", nil), pkgName];

				// create package entry ref
				CPtrPipe pipe;
				pipe.init((void *)pkg.refData.bytes, pkg.refData.length, NO, NULL);
				RefVar pkgEntry(UnflattenRef(pipe));

				// create package data -- pkgEntry.pkgRef -> NSData
				RefVar pkgRef(GetFrameSlot(pkgEntry, MakeSymbol("pkgRef")));
				if (IsBinary(pkgRef))
				{
					NSData * pkgData;
					WITH_LOCKED_BINARY(pkgRef, pkgPtr)
					pkgData = [NSData dataWithBytesNoCopy:pkgPtr length:Length(pkgRef) freeWhenDone:NO];
					END_WITH_LOCKED_BINARY(pkgRef)
					// send that package data
					[session sendEvent:kDLoadPackage data:pkgData.bytes length:pkgData.length];
					evt = [session receiveEvent: kDAnyEvent];	// kDResult | kDOperationCanceled
					if (evt.tag == kDResult
					 && evt.value != noErr)
					{
						REPprintf("\n#### Error %d restoring package.\n", err);
						break;
					}
					if (evt.tag == kDOperationCanceled)
					{
						[session sendEvent:kDOpCanceledAck];
						newtonCancelled = YES;
						break;
					}
				}
			}
#endif
		END_FOREACH	// store
	}
	newton_catch_all
	{
		err = (NewtonErr)(long)CurrentException()->data;
		REPprintf("\n#### Exception %s (%d) while backing up.\n", CurrentException()->name, err);
	}
	end_try;

	[self.dock restoreDone:err];
	[self.dock disconnect];
}


@end
