/*
	File:		BackupComponent.mm

	Contains:	The NCX backup/restorecomponent controller.

	Written by:	Newton Research, 2009.
*/

#import "BackupComponent.h"
#import "NCDocument.h"
#import "IdList.h"
#import "Utilities.h"
#import "PreferenceKeys.h"
#import "NCXErrors.h"
#import "Logging.h"


/* -----------------------------------------------------------------------------
	Declarations.
----------------------------------------------------------------------------- */
DeclareException(exComm, exRootException);

extern "C" Ref	ArrayInsert(RefArg ioArray, RefArg inObj, long index);
extern "C" Ref FFindStringInArray(RefArg inRcvr, RefArg inArray, RefArg inStr);
extern "C" Ref FStrEqual(RefArg inRcvr, RefArg inStr1, RefArg inStr2);
extern "C" int	REPprintf(const char * inFormat, ...);
extern "C" void REPflush(void);

#define kFileChunkSize 4*KByte


/*------------------------------------------------------------------------------
	B a c k u p F i l e H e a d e r
------------------------------------------------------------------------------*/

struct SourceInfo
{
	uint32_t version;
	uint32_t manufacturer;
	uint32_t machineType;
};

struct BackupFileHeader
{
	char signature[8];
	SourceInfo source;
};


/* -----------------------------------------------------------------------------
	N C B a c k u p C o m p o n e n t
----------------------------------------------------------------------------- */

#pragma mark -
@implementation NCBackupComponent
/*------------------------------------------------------------------------------
	Here’s how it goes:
	desktop	requests list of stores and apps to backup
	newton	returns a frame containing that info
	desktop	requests backup with that info - ALL of it
	newton	creates backup file and writes soup entries to it
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
	Return the event command tags handled by this component.
	Args:		--
	Return:	array of strings
------------------------------------------------------------------------------*/

- (NSArray *)eventTags {
	return [NSArray arrayWithObjects:@"ssyn",	// kDRequestToSync
												@"SSYN",	// kDDoRequestToSync
												nil ];
}


#pragma mark Newton Event Handlers
/*------------------------------------------------------------------------------
	Newton requests back up. We need to fetch the sync options,
	ie what to back up, then go do it.
				kDRequestToSync
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_ssyn:(NCDockEvent *)inEvent {
	self.dock.operationInProgress = kBackupActivity;
	[self.dock resetProgressFor:self];

	[self doBackup];
}


#pragma mark Desktop Event Handlers
/*------------------------------------------------------------------------------
	Desktop requests back up. We need to request a sync operation with Newton,
	fetch the sync options just to find what stores to back up, force back up of
	all apps and packages, then go do it.
				kDDoRequestToSync
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_SSYN:(NCDockEvent *)inEvent {
	NewtonErr err = noErr;
	newton_try
	{
// put desktop in control
		[self.dock setDesktopControl:YES];

		[self.dock.session sendEvent:kDRequestToSync];
		err = [self.dock.session receiveResult];
		THROWIF(err, exComm);
	}
	newton_catch_all
	{
		err = (NewtonErr)(long)CurrentException()->data;
		REPprintf("\n#### Exception %s (%d) while backing up.\n", CurrentException()->name, err);
	}
	end_try;

	if (err)
		[self.dock syncDone:err];
	else
		[self doBackup];
}


#pragma mark -
/*------------------------------------------------------------------------------
	Back up everything. No point in omitting anything.
	This is the way iTunes does it -- admittedly it has a faster connection.
	Get soup entries for all stores and backup to a CoreData persistent store.
	Crucial slots in the store frame:
		name: "Internal",
		apps: [{name:"xxx", soups:["yyy"]},..],
		soups: ["Names","Notes",..],	| aligned so name maps to signature
		signatures: [1,2,3,..]			|
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/
- (NSProgress *)setupProgress {
	self.progress = [NSProgress progressWithTotalUnitCount:-1];
	self.progress.localizedDescription = @"Fetching sync options…";
	return self.progress;
}


- (void) doBackup
{
	NewtonErr err = noErr;
	NCDocument * document = self.dock.document;

@try
{
	[document makeManagedObjectContextForThread];
	newton_try
	{
		NCDockEvent * evt = [self.dock.session sendEvent:kDGetSyncOptions expecting:kDSyncOptions];
		RefVar syncInfo(evt.ref);
FULL_LOG {
	REPprintf("\nsyncInfo:\n");
	PrintObject(syncInfo, 0);
}
		// fake -- just to get the current time | do we really need this?
		/*NewtonTime currentTime =*/[self.dock.session setLastSyncTime:0];

		BOOL isPackagesReqd = NOTNIL(GetFrameSlot(syncInfo, MakeSymbol("packages")));
		RefVar allStores(GetFrameSlot(syncInfo, MakeSymbol("stores")));
		if (!IsArray(allStores))
			ThrowErr(exStore, kStoreErrStoreNotFound);

		//	set up progress indicator -- count all apps on all stores
		unsigned appIndex = 0, numOfApps = 0;
		FOREACH(allStores, store)
			RefVar selectedApps(GetFrameSlot(store, MakeSymbol("apps")));
			if (!IsArray(selectedApps))
			{
				// get [{name:"xxx", soups:["yyy"]},..] of apps on this store
				[self.dock.session setCurrentStore:store info:NO];
				[self.dock.session sendEvent:kDGetAppNames value:kNamesAndSoupsForThisStore];
				evt = [self.dock.session receiveEvent:kDAppNames];
				selectedApps = evt.ref;
				SetFrameSlot(store, MakeSymbol("apps"), selectedApps);
			}
			numOfApps += Length(selectedApps);
		END_FOREACH
		self.progress.completedUnitCount = 0;
		self.progress.totalUnitCount = numOfApps;

		FOREACH(allStores, store)
FULL_LOG {
	REPprintf("\nBacking up store\n");
	PrintObject(store, 0);
}
			// the store must already exist in our document
			NCStore * storeObj = [document findStore: store];
			NSString * storeObjName = storeObj.name;
			self.progress.localizedDescription = [NSString stringWithFormat:@"%@: fetching app and soup names…", storeObjName];
			// set current store
			[self.dock.session setCurrentStore:store info:NO];

			// get matching arrays of soupName, soupSignature for all soups on this store
			// we can’t rely on the store.soups and store.signatures slots
			RefVar soupNames(GetFrameSlot(store, SYMA(soups)));
			RefVar soupSignatures(GetFrameSlot(store, MakeSymbol("signatures")));
			if (!IsArray(soupSignatures))
			{
				soupSignatures = [self.dock.session getAllSoups];
				soupNames = GetArraySlot(soupSignatures, 0);
				soupSignatures = GetArraySlot(soupSignatures, 1);
			}

			// create array of apps for backup
			// - move System information app item to the start
			// - if there’s a packages app item it should go last
			RefVar appsOnThisStore(GetFrameSlot(store, MakeSymbol("apps")));
			RefVar systemApp;
			RefVar packagesApp;
FULL_LOG {
	REPprintf("\napps on this store:\n");
	PrintObject(appsOnThisStore, 0);
}
			FOREACH(appsOnThisStore, app)
				if (EQRef(ClassOf(app), MakeSymbol("packageFrame"))		// for OS 2.1
				||  NOTNIL(GetFrameSlot(app, MakeSymbol("packages"))))	// for OS 2.0
					packagesApp = app;
				else if (EQRef(ClassOf(app), MakeSymbol("systemFrame")))
					systemApp = app;
			END_FOREACH	// app
			if (NOTNIL(packagesApp))
			{
				ArrayRemove(appsOnThisStore, packagesApp);
				if (isPackagesReqd)
					AddArraySlot(appsOnThisStore, packagesApp);
			}
			if (NOTNIL(systemApp))
			{
				ArrayRemove(appsOnThisStore, systemApp);
				ArrayInsert(appsOnThisStore, systemApp, 0);
			}

// and this is where our story really starts…

			unwind_protect
			{
				// backup the apps
				FOREACH(appsOnThisStore, app)
					NSString * appName = MakeNSString(GetFrameSlot(app, SYMA(name)));
					NCApp * appObj = [document findApp: appName];	//	will create app if necessary
// -- update UI sidebar APPS

					RefVar appSoups(GetFrameSlot(app, SYMA(soups)));
					FOREACH(appSoups, soupName)
FULL_LOG {
	REPprintf("\n");
	PrintObject(soupName, 0);
}
						if ([self.dock.session setCurrentSoup:soupName] == noErr)
						{
							NSString * statusStr = [NSString stringWithFormat: NSLocalizedString(@"backing up", nil), storeObjName, appName];
							if (Length(appSoups) > 1)
								statusStr = [NSString stringWithFormat:@"%@ : %@ soup", statusStr, MakeNSString(soupName)];
							self.progress.localizedDescription = statusStr;
							// look up soup’s signature from its name
							int soupSignature = 0;
							Ref sigIndex;
							if (ISINT(sigIndex = FFindStringInArray(RA(NILREF), soupNames, soupName)))
								soupSignature = (int)RVALUE(GetArraySlot(soupSignatures, (ArrayIndex)RVALUE(sigIndex)));
//REPprintf(" = soupNames[%d] -> %d\n",ISINT(sigIndex)?RVALUE(sigIndex):-1, soupSignature);

							RefVar soupInfo, soupIndex;
							NewtonTime syncTime = 0;
							EventType result = 0;

							NCSoup * soupObj;
							if ((soupObj = [storeObj findSoup:MakeNSString(soupName)]))
							{
								// soup exists -- update it
								syncTime = [self.dock.session setLastSyncTime:soupObj.lastSyncTime];
								XTRY
								{
									// if soup info has changed, update it
									evt = [self.dock.session sendEvent:kDGetChangedInfo expecting:kDAnyEvent];
									XFAILIF(evt.tag == kDOperationCanceled, result = kDOperationCanceled;)	// user cancelled
									if (evt.tag == kDSoupInfo)
									{
										soupInfo = evt.ref;
										[soupObj updateInfo:soupInfo];
									}
	
									// if soup index has changed, update it
									evt = [self.dock.session sendEvent:kDGetChangedIndex expecting:kDAnyEvent];
									XFAILIF(evt.tag == kDOperationCanceled, result = kDOperationCanceled;)	// user cancelled
									if (evt.tag == kDIndexDescription)
									{
										soupIndex = evt.ref;
										[soupObj updateIndex:soupIndex];
									}

									soupObj.signature = [NSNumber numberWithInt:soupSignature];

									BOOL isPackagesSoup = soupObj.app.isPackages;
									NCIdList * idList = [[NCIdList alloc] init];

									[self.dock.session sendEvent:kDBackupSoup value:[soupObj.lastBackupId unsignedIntValue]];
									for (;;)
									{
										evt = [self.dock.session receiveEvent:kDAnyEvent];
										// expecting kDSoupNotDirty, kDEntry, kDSetBaseID, kDBackupIDs, kDBackupSoupDone, kDOperationCanceled
										if (evt.tag == kDEntry)
										{
											if (isPackagesSoup)
											{
												RefVar entry(evt.ref);
												if (FrameHasSlot(entry, MakeSymbol("pkgRef")))
												{
													NSString * pkgName = MakeNSString(GetFrameSlot(entry, MakeSymbol("packageName")));
													self.progress.localizedDescription = [NSString stringWithFormat:NSLocalizedString(@"backing up package", nil), storeObjName, pkgName];

													NCEntry * entryObj = [soupObj addEntry:entry withNSOFData:evt.data length:evt.dataLength];
													[idList addId:[entryObj.uniqueId unsignedIntValue]];
												}
											}
											else
											{
												NCEntry * entryObj = [soupObj addEntry:evt.ref withNSOFData:evt.data length:evt.dataLength];
												[idList addId:[entryObj.uniqueId unsignedIntValue]];
											}
										}
										else if (evt.tag == kDSetBaseID)
										{
											// set base for subsequent kDBackupIDs
											[idList setBaseId:evt.value];
										}
										else if (evt.tag == kDBackupIDs)
										{
											// add ids to our list - these entries have not been modified or added
											short * codedId;
											for (codedId = (short *)evt.data; [idList add:CANONICAL_SHORT(*codedId)]; codedId++)
												;
										}
										else if (evt.tag == kDBackupSoupDone
											  ||  evt.tag == kDSoupNotDirty)
										{
											result = evt.tag;
											break;
										}
										else if (evt.tag == kDOperationCanceled)
										{
											[self.dock.session sendEvent:kDOpCanceledAck];
											result = evt.tag;
											break;
										}
										if (self.progress.isCancelled)
										{
											[self.dock.session sendEvent:kDOperationCanceled /*expecting:kDOpCanceledAck*/];
											result = kDOperationCanceled;
											break;
										}
									}

									NSIndexSet * newtIds = idList.ids;
	/* ---- delete entries locally that were deleted from Newton ---- */
									if (result == kDBackupSoupDone)
										[soupObj cropTo:newtIds];

									if (result == kDBackupSoupDone
									||  result == kDSoupNotDirty)
									{
										if (isPackagesSoup)
										{
	/* ---- we DON’T remove packages that were deleted locally ---- */
	/* ---- packages can’t be deleted locally ---- */
	/* ---- problem is, for packages, when you -getEntry:idx you don’t necessarily get back the entry with that index: an extras icon entry will redirect you to the actuak package ---- */
#if 0
											NSIndexSet * localIds = soupObj.currIds;
											for (NSUInteger idx = [newtIds firstIndex]; idx != NSNotFound; idx = [newtIds indexGreaterThanIndex:idx])
											{
												if (![localIds containsIndex:idx])
												{
													// fetch entry w/ id = idx
													Ref pkgEntry = [self.dock.session getEntry:idx];
													// if it’s pkgRef, extract name, rmvp
													NSString * pkgName = PackageName(pkgEntry);
													if (pkgName)
													{
														self.progress.localizedDescription = [NSString stringWithFormat:NSLocalizedString(@"removing package", nil), storeObjName, pkgName];

														[self.dock.session sendEvent:kDRemovePackage ref:MakeString(pkgName)];
														err = [self.dock.session receiveResult];
														// ignore any errors, just keep going
													}
												}
												if (self.progress.isCancelled)
												{
													result = kDOperationCanceled;
													break;
												}
											}

	/* ---- we DON’T install packages that were imported locally ---- */
	/* ---- packages can’t be imported locally ---- */
#endif
										}
										else
										{
	/* ---- delete entries that were deleted locally ---- */
											NSIndexSet * localIds = soupObj.currIds;
											RefVar didList(MakeArray(0));
											for (NSUInteger idx = [newtIds firstIndex]; idx != NSNotFound; idx = [newtIds indexGreaterThanIndex:idx])
											{
												if (![localIds containsIndex:idx])
													AddArraySlot(didList, MAKEINT(idx));
											}
											ArrayIndex numOfEntries = Length(didList);
											if (numOfEntries > 0)
											{
												NSString * item = (numOfEntries == 1) ? @"entry" : @"entries";
												self.progress.localizedDescription = [NSString stringWithFormat:@"Synchronising: deleting %d %@",numOfEntries,item];
												[self.dock.session deleteEntryIdList:didList];
											}

	/* ---- send entries that were imported locally ---- */
											NSArray * importedEntries = [soupObj importedEntries];
											numOfEntries = importedEntries ? (ArrayIndex)importedEntries.count : 0;
											if (numOfEntries > 0)
											{
												NSString * item = (numOfEntries == 1) ? @"entry" : @"entries";
												self.progress.localizedDescription = [NSString stringWithFormat:@"Synchronising: adding %d %@",numOfEntries,item];
												for (NCEntry * entry in importedEntries)
												{
													RefVar ref(entry.ref);
													XFAIL(err = [self.dock.session addEntry:ref])
													// self.dock.session addEntry: updates ref w/ its added _uniqueId and _modTime
													[entry update:ref];
												}
											}
										}
									}
									idList = nil;
								}
								XENDTRY;
							}
							else
							{
								// we don’t know about this soup yet
								syncTime = [self.dock.session setLastSyncTime:0];
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
// -- make new soup
									soupObj = [storeObj addSoup:MakeNSString(soupName) signature:soupSignature indexes:soupIndex info:soupInfo];
FULL_LOG {
	REPprintf("\n---- making new soup ----\nName: ");
	PrintObject(soupName, 0);
	REPprintf(" %u\nIndexes: ", soupSignature);
	PrintObject(soupIndex, 0);
	REPprintf("\nInfo: ");
	PrintObject(soupInfo, 0);
	REPprintf("\n---- see console log ----");
	REPflush();
	NSLog(@"---- NCX log begins ----");
	NSLog(@"Creating soup: %@", soupObj);
	NSLog(@"Belonging to app: %@", appObj);
	NSLog(@"On store: %@", storeObj);
	NSLog(@"---- NCX log ends ----");
}
									[document app:appObj addSoup:soupObj];

									BOOL isPackagesSoup = soupObj.app.isPackages;

									[self.dock.session sendEvent:kDSendSoup];
									for (;;)
									{
										evt = [self.dock.session receiveEvent:kDAnyEvent];
										// expecting kDEntry, kDBackupSoupDone, kDOperationCanceled
										if (evt.tag == kDEntry)
										{
											RefVar entry(evt.ref);
FULL_LOG {
	REPprintf("\n---- adding new entry ----\n");
	PrintObject(entry, 0);
}
											if (isPackagesSoup)
											{
												if (FrameHasSlot(entry, MakeSymbol("pkgRef")))
												{
													NSString * pkgName = MakeNSString(GetFrameSlot(entry, MakeSymbol("packageName")));
													self.progress.localizedDescription = [NSString stringWithFormat:NSLocalizedString(@"backing up package", nil), storeObjName, pkgName];

													[soupObj addEntry:entry withNSOFData:evt.data length:evt.dataLength];
												}
											}
											else
											{
												[soupObj addEntry:entry withNSOFData:evt.data length:evt.dataLength];
											}
										}
										else if (evt.tag == kDBackupSoupDone)
										{
											result = evt.tag;
											break;
										}
										else if (evt.tag == kDOperationCanceled)
										{
											[self.dock.session sendEvent:kDOpCanceledAck];
											result = evt.tag;
											break;
										}
										if (self.progress.isCancelled)
										{
											[self.dock.session sendEvent:kDOperationCanceled /*expecting:kDOpCanceledAck*/];
											result = kDOperationCanceled;
											break;
										}
									}
								}
								XENDTRY;
							}

							// good time to save the document?
							[document savePersistentStore];
FULL_LOG {
	REPflush();
}
							// cancel this loop
							if (result == kDOperationCanceled)
							{
								ThrowErr(exStore, kNCErrOperationCancelled);
							}

							else
							{
								// success
								if (ISNIL(soupInfo))
									soupInfo = soupObj.infoFrame;
								SetFrameSlot(soupInfo, MakeSymbol("NCKLastBackupTime"), MAKEINT(syncTime+1));
								[soupObj updateInfo:soupInfo];
								[self.dock.session setSoupInfo:soupInfo];
							}
						}
else REPprintf("\n#### soup not on store!");
					END_FOREACH	// soupName
					self.progress.completedUnitCount = ++appIndex;
				END_FOREACH	// app

				// at this point we have a full backup on this store
			}
			on_unwind
			{
			}
			end_unwind;
		END_FOREACH	// store
	}
	newton_catch_all
	{
		err = (NewtonErr)(long)CurrentException()->data;
		REPprintf("\n#### Exception %s (%d) while backing up.\n", CurrentException()->name, err);
	}
	end_try;

}
@catch (NSException * exception)
{
	err = kNCErrException;
	document.exceptionStr = [exception reason];
}

	[document disposeManagedObjectContextForThread];
	[self.dock syncDone:err];
}

@end


#pragma mark -
@implementation NCRestoreComponent
/*------------------------------------------------------------------------------
	D e s k t o p   I n t e r f a c e

	We don’t use per-store backup files any more; there’s a CoreData database per device.
	So Restore from Newton end will need a little work, but at the Mac end:
	The Restore button brings up a sheet for selecting which stores in the tethered Newt to restore
	and whether to include packages.
	There is no other selective restore. If the user wants to copy app|soup|data from another device database
	they must drag it over. We should also allow deleting apps|soups|entries.
	Once selection is made, restore proceeds pretty much as before, except we’re reading from the db,
	not a stream.

	Sort out
		Desktop | Newton Dock Interface sections
		setup, start methods

	<user presses Restore button>
	appController: start 'RSTR'
		self: enableButtons NO
		session: start 'RSTR'
			gDockEventQueue: makeEvent 'RSTR'

	component: do_RSTR
		choose backup file  <UI>
		choose what to restore from backup file  <UI>
		confirm restore  <UI>
		set up progress  <UI>
		[dock setDesktopControl:YES];
		[session send: kDRequestToRestore];
		[session readCommand: YES];	// kDResult
		doRestore
		session: setDesktopControl NO
		session: send kDOperationDone	// no reply expected
		done

	<Newton: user browses for backup file to restore>
	<Newton: user initiates restore>
	component: do_kDRestoreFile
		set up progress  <UI>
		self: enableButtons NO
		session: send kDResult
	<Newton: user chooses what to restore>
	component: do_kDRestoreOptions
		set up restore info from event data
		doRestore
		session: send kDOperationDone	// no reply expected
		done

------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
	Return the event command tags handled by this component.
	Args:		--
	Return:	array of strings
------------------------------------------------------------------------------*/

- (NSArray *) eventTags
{
	return [NSArray arrayWithObjects: @"rsfl",	// kDRestoreFile
												 @"grop",	// kDGetRestoreOptions
												 @"ropt",	// kDRestoreOptions
												 @"rall",	// kDRestoreAll
												 @"RSTR",	// kDDoRestore
												 nil ];
}


/*------------------------------------------------------------------------------
	N e w t o n   D o c k   I n t e r f a c e
------------------------------------------------------------------------------*/

#pragma mark Newton Event Handlers
/*------------------------------------------------------------------------------
	Restore from backup file.
	The file path has already been browsed from the Newton.
				kDRestoreFile
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void) do_rsfl: (NCDockEvent *) inEvent
{
	NewtonErr err = noErr;
	RefVar filename(inEvent.ref);
	if (IsFrame(filename))
		filename = GetFrameSlot(filename, SYMA(name));

	self.dock.operationInProgress = kRestoreActivity;
	[self.dock resetProgressFor:self];

	[self setRestorePath: [[self.dock filePath:filename] path]];
	// check backup file is valid and set result accordingly
	if (restorePath)
		; // hunky dory
	else
		err = -1;
	[self.dock.session sendEvent:kDResult value:err];
}


/*------------------------------------------------------------------------------
	Newton requests info on the selected backup file.
				kDGetRestoreOptions
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void) do_grop: (NCDockEvent *) inEvent
{
	BOOL isInternalStore = [MakeNSString(GetFrameSlot(syncInfo, SYMA(kind))) isEqualToString: @"Internal"];
	int storeType = isInternalStore ? kRestoreToNewton : kRestoreToCard;
	RefVar pkgs(MakeArray(0));
	RefVar apps(MakeArray(0));

	RefVar app, allApps(GetFrameSlot(syncInfo, MakeSymbol("apps")));
	for (ArrayIndex i = 1, count = Length(allApps); i < count; ++i)	// don’t list first System Information
	{
		app = GetArraySlot(allApps, i);
		AddArraySlot(apps, GetFrameSlot(app, SYMA(name)));
	}

	RefVar restoreWhich(AllocateFrame());
	SetFrameSlot(restoreWhich, MakeSymbol("storeType"), MAKEINT(storeType));
	SetFrameSlot(restoreWhich, MakeSymbol("packages"), pkgs);
	SetFrameSlot(restoreWhich, MakeSymbol("applications"), apps);

	[self.dock.session sendEvent:kDRestoreOptions ref:restoreWhich];
}


/*------------------------------------------------------------------------------
	Record which items have been selected for restore.
				kDRestoreOptions
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void) do_ropt: (NCDockEvent *) inEvent
{
	RefVar restoreWhich(inEvent.ref);
	RefVar apps(GetFrameSlot(restoreWhich, MakeSymbol("applications")));

	// filter out syncInfo.apps that are NOT in this apps array
	RefVar deselectedSym(MakeSymbol("not-me"));
	RefVar app, allApps(GetFrameSlot(syncInfo, MakeSymbol("apps")));
	for (ArrayIndex i = 0, count = Length(allApps); i < count; ++i)
	{
		app = GetArraySlot(allApps, i);
		if (ISNIL(FFindStringInArray(RA(NILREF), apps, GetFrameSlot(app, SYMA(name))))
		||  i == 0)	// don’t list first System Information
			SetFrameSlot(app, deselectedSym, RA(TRUEREF));
	}

	[self doRestore];
}


/*------------------------------------------------------------------------------
	Restore everything.
				kDRestoreAll
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void) do_rall: (NCDockEvent *) inEvent
{
	RefVar merge(inEvent.ref);
	// don’t know what to do with this

	[self doRestore];
}


#pragma mark Desktop Event Handlers
/*------------------------------------------------------------------------------
	Initiate restore from desktop.
				kDDoRestore
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void) do_RSTR: (NCDockEvent *) inEvent
{
	NewtonErr err = noErr;

	newton_try
	{
	// put desktop in control
		[self.dock setDesktopControl:YES];
		[self.dock.session sendEvent:kDRequestToRestore];
		err = [self.dock.session receiveResult];
		THROWIF(err, exComm);
	}
	newton_catch_all
	{
		err = (NewtonErr)(long)CurrentException()->data;
		REPprintf("\n#### Exception %s (%d) while restoring.\n", CurrentException()->name, err);
	}
	end_try;

	if (err)
		[self.dock restoreDone:err];
	else
		[self doRestore];
}


/*------------------------------------------------------------------------------
	Set the path to the backup file to be restored.
	Verify the file while we’re about it.
	Args:		inPath			full path to the backup file
	Return:	--
------------------------------------------------------------------------------*/

- (void) setRestorePath: (NSString *) inPath
{
	syncInfo = NILREF;
	restorePath = NULL;

	// verify file contains backup info
	newton_try
	{
		const char * pathStr = inPath.fileSystemRepresentation;
		CStdIOPipe pipe(pathStr, "r");
		char signature[8];
		size_t chunkSize = 8;
		bool isEOF;
		pipe.readChunk(&signature[0], chunkSize, isEOF);
		if (strncmp(signature, "backup0", 7) == 0)
		{
			pipe.readSeek(sizeof(BackupFileHeader), SEEK_SET);	// skip past header
			syncInfo = UnflattenRef(pipe);
			restorePath = (char *)malloc(strlen(pathStr)+1);
			strcpy(restorePath, pathStr);
		}
	}
	newton_catch_all
	{
		syncInfo = NILREF;
		if (restorePath)
			free(restorePath), restorePath = NULL;
	}
	end_try;
}


#pragma mark -
/*------------------------------------------------------------------------------
	Restore soups and entries for selected backup file store.
	
	Assume the document holds the selection to restore: document.restoreInfo
	a dictionary of the form:
		{ store: { name:"Internal" (= NCStore*) },
		  restore: { store: { name:"Internal" (= NCStore*) },
					    apps: [ { isSelected: YES, name: "Names" (= NCApp*) },..],
					    pkgs: [ { isSelected: NO, title: "Newton Devices" (= NCEntry*) },..] } }

	Args:		--
	Return:	--
------------------------------------------------------------------------------*/
- (NSProgress *)setupProgress {
	self.progress = [NSProgress progressWithTotalUnitCount:-1];
	self.progress.localizedDescription = @"Preparing to restore…";
	return self.progress;
}


- (void) doRestore
{
	NewtonErr err = noErr;
	NCDocument * document = self.dock.document;

	newton_try
	{
		RefVar data;
		NCDockEvent * evt;

		// send source info
		NewtonInfo * newtonInfo = (NewtonInfo *)document.deviceObj.info.bytes;
		SourceInfo source;
		source.version = (newtonInfo->fROMVersion < 0x00020000) ? kOnePointXData : kTwoPointXData;
		source.manufacturer = newtonInfo->fManufacturer;
		source.machineType = newtonInfo->fMachineType;
#if defined(hasByteSwapping)
		source.version = BYTE_SWAP_LONG(source.version);
		source.manufacturer = BYTE_SWAP_LONG(source.manufacturer);
		source.machineType = BYTE_SWAP_LONG(source.machineType);
#endif
		[self.dock.session sendEvent:kDSourceVersion data:&source length:sizeof(SourceInfo)];
		err = [self.dock.session receiveResult];
		THROWIF(err, exComm);

//		[self installPackageExtensions];	// in the original - but do we use this here at all?

		NCStore * storeObj = [document.restoreInfo.store objectForKey:@"store"];
		NSString * storeObjName = storeObj.name;
		NSPredicate * selectedPredicate = [NSPredicate predicateWithFormat:@"isSelected = YES"];
		NSArray * selectedApps = [document.restoreInfo.apps filteredArrayUsingPredicate:selectedPredicate];
		NSArray * selectedPkgs = [document.restoreInfo.pkgs filteredArrayUsingPredicate:selectedPredicate];

		//	set up progress indicator
		ArrayIndex appIndex = 0;
		NSUInteger numOfApps = selectedApps.count + selectedPkgs.count;
		self.progress.completedUnitCount = 0;
		self.progress.totalUnitCount = numOfApps;
		self.progress.localizedDescription = [NSString stringWithFormat:@"Restoring %@", storeObjName];	// storeObj from DEVICE

		// set current store
		err = [self.dock.session setCurrentStore:storeObj.ref info:NO];	// storeObj from DEVICE
		if (err == -28001)	// Invalid store signature
		{
#if 0
		// user probably erased store -- its signature has changed.
		//	look in allStores for the closest match (same name? kind?) and set that store
			RefVar newStore;
			FOREACH(allStores, testStore)
				if (NOTNIL(FStrEqual(RA(NILREF), GetFrameSlot(testStore, SYMA(name)), storeName)))
				{
					newStore = testStore;
					break;
				}
			END_FOREACH;
			if (NOTNIL(newStore))
				err = [self.dock.session setCurrentStore:newStore info:NO];
#endif
		}
		if (err != noErr)
			ThrowErr(exStore, err);

		for (NCApp * appObj in selectedApps)
		{
			NSString * appObjName = appObj.name;
			self.progress.localizedDescription = [NSString stringWithFormat: NSLocalizedString(@"restoring", nil), storeObjName, appObjName];

			// iterate over soups in app
			for (NCSoup * soupObj in [document.restoreInfo.sourceStore soupsInApp: appObj])	// storeObj from LIBRARY
			{
				// if the soup already exists, delete it
				RefVar soupName(MakeString(soupObj.name));
				if ([self.dock.session setCurrentSoup:soupName] == noErr)
					[self.dock.session deleteSoup];

FULL_LOG {
	REPprintf("\n");
	PrintObject(soupName, 0);
}
				// create it afresh
				[self.dock.session createSoup:soupName index:soupObj.indexArray];
				[self.dock.session setCurrentSoup:soupName];
				// set its info
				[self.dock.session setSoupInfo:soupObj.infoFrame];
				// set its signature
				[self.dock.session sendEvent:kDSetSoupSignature value:[soupObj.signature intValue]];
				err = [self.dock.session receiveResult];
				// do something with result

				// iterate over entries in the soup
				for (NCEntry * entry in [soupObj orderedEntries])
				{
					[self.dock.session sendEvent:kDAddEntryWithUniqueID data:entry.refData.bytes length:(unsigned int)entry.refData.length];
					evt = [self.dock.session receiveEvent:kDAnyEvent];	// kDResult | kDOperationCanceled
					if (evt.tag == kDResult
					 && evt.value != noErr)
					{
						REPprintf("\n#### Error %d restoring soup entry.\n", err);
						break;
					}
					if (evt.tag == kDOperationCanceled)
					{
						[self.dock.session sendEvent:kDOpCanceledAck];
						ThrowErr(exStore, kNCErrOperationCancelled);
						break;
					}
					if (self.progress.isCancelled)
					{
						[self.dock.session sendEvent:kDOperationCanceled /*expecting:kDOpCanceledAck*/];
						ThrowErr(exStore, kNCErrOperationCancelled);
						break;
					}
				}
			}	// foreach soup
			self.progress.completedUnitCount = ++appIndex;
		}	// foreach app

FULL_LOG {
	REPprintf("\nPackages\n");
}
		// assume the Packages soup exists -- we don’t really have the authority to create it
		//-- or -- wouldn’t a proper restore do better to delete/create the soup like any other? (* if (selectedPkgs.count > 0) *)
		//			  then no need to kDRemovePackage

		// iterate over packages
		for (NCEntry * pkg in selectedPkgs)
		{
			NSString * pkgName = pkg.title;
			self.progress.localizedDescription = [NSString stringWithFormat: NSLocalizedString(@"restoring package", nil), storeObjName, pkgName];

			//	delete pkg (if it exists?)
FULL_LOG {
	REPprintf("installing %s\n",[pkgName UTF8String]);
}
			[self.dock.session sendEvent:kDRemovePackage ref:MakeString(pkgName)];
			err = [self.dock.session receiveResult];
			// do something with result!
			//	install pkg
			CPtrPipe pipe;
			pipe.init((void *)pkg.refData.bytes, pkg.refData.length, NO, NULL);
			RefVar pkgEntry(UnflattenRef(pipe));
			RefVar pkgRef(GetFrameSlot(pkgEntry, MakeSymbol("pkgRef")));
			if (IsBinary(pkgRef))
			{
				NSData * pkgData;
				WITH_LOCKED_BINARY(pkgRef, pkgPtr)
				pkgData = [[NSData alloc] initWithBytesNoCopy:pkgPtr length:Length(pkgRef) freeWhenDone:NO];
				END_WITH_LOCKED_BINARY(pkgRef)
				[self.dock.session sendEvent:kDLoadPackage data:pkgData.bytes length:(unsigned int)pkgData.length];
				pkgData = nil;
				evt = [self.dock.session receiveEvent:kDAnyEvent];	// kDResult | kDOperationCanceled
				if (evt.tag == kDResult
				 && (err = evt.value) != noErr)
				{
					REPprintf("\n#### Error %d restoring package.\n", err);
					break;
				}
				if (evt.tag == kDOperationCanceled)
				{
					[self.dock.session sendEvent:kDOpCanceledAck];
					err = kNCErrOperationCancelled;
					break;
				}
				if (self.progress.isCancelled)
				{
					[self.dock.session sendEvent:kDOperationCanceled /*expecting:kDOpCanceledAck*/];
					err = kNCErrOperationCancelled;
					break;
				}
			}
			self.progress.completedUnitCount = ++appIndex;
		}	// foreach pkg
	}
	newton_catch_all
	{
		err = (NewtonErr)(long)CurrentException()->data;
		REPprintf("\n#### Exception %s (%d) during restore.\n", CurrentException()->name, err);
	}
	end_try;

	syncInfo = NILREF;
	if (restorePath)
		free(restorePath), restorePath = NULL;

	[self.dock restoreDone:err];
}


@end
