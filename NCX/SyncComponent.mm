/*
	File:		SyncComponent.mm

	Contains:   Sync methods for NCX.

	Written by: Newton Research Group, 2006.
*/

#import "SyncComponent.h"
#import "NCDockProtocolController.h"
#import "NCDocument.h"
#import "IdList.h"
#import "Utilities.h"
#import "NCXErrors.h"

#import "NameTransformer.h"
#import "DateTransformer.h"

#define kDDoSync					'DSYN'
#define kDDoSyncPull				'SYNP'
#define kDDoSyncDone				'SYND'


/* -----------------------------------------------------------------------------
	Declarations.
----------------------------------------------------------------------------- */
DeclareException(exComm, exRootException);

extern "C" {
Ref FFindStringInArray(RefArg inRcvr, RefArg inArray, RefArg inStr);
}


#pragma mark -
/* -----------------------------------------------------------------------------
	Ids.
----------------------------------------------------------------------------- */

NSString *
MakeStoreId(unsigned index)
{
	return (index == 0) ? @"internal" : [NSString stringWithFormat: @"card%d", index];
}


NSString *
StoreIdFrom(NSString * inRecordId)
{
	NSArray * idParts = [inRecordId componentsSeparatedByString: @"-"];
	return ([idParts count] > 0) ? [idParts objectAtIndex: 0] : nil;
}


NSString *
EntryIdFrom(NSString * inRecordId)
{
	NSArray * idParts = [inRecordId componentsSeparatedByString: @"-"];
	return ([idParts count] == 3) ? [idParts objectAtIndex: 2] : nil;
}


NSString *
MainEntityIdFrom(NSString * inRecordId)
{
	NSArray * idParts = [inRecordId componentsSeparatedByString: @"-"];
	return ([idParts count] > 2) ? [[idParts subarrayWithRange: (NSRange){0,3}] componentsJoinedByString: @"-"] : nil;
}


BOOL
IsMainEntityId(NSString * inRecordId)
{
	NSArray * idParts = [inRecordId componentsSeparatedByString: @"-"];
	return [idParts count] == 3;
}


@interface NSString (Syncer)
- (BOOL) isInDomain: (NSString *) inStr;
@end


@implementation NSString (Syncer)

- (BOOL) isInDomain: (NSString *) inStr;
{
	NSArray * parts1 = [self componentsSeparatedByString: @"-"];
	NSArray * parts2 = [inStr componentsSeparatedByString: @"-"];
	
	return [parts1 count] == [parts2 count]
		 && [parts1 count] > 0
		 && [[parts1 objectAtIndex: 0] isEqualToString: [parts2 objectAtIndex: 0]];
}

@end


@implementation NCSyncComponent

/* -----------------------------------------------------------------------------
	Initialize ivars for new connection session.
	Args:		inSession
	Return:	--
----------------------------------------------------------------------------- */

- (id) initWithProtocolController: (NCDockProtocolController *) inController
{
	if (self = [super initWithProtocolController: inController])
	{
		isDesktopInControl = NO;
		isSyncExtensionInstalled = NO;
	}
	return self;
}


/* -----------------------------------------------------------------------------
	If we haven’t loaded the sync extension then load it now.
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (void) installSyncExtensions
{
	if (!isSyncExtensionInstalled)
	{
		[session loadExtension: @"eNsp"];
		isSyncExtensionInstalled = YES;
	}
}


/* -----------------------------------------------------------------------------
	Return the event command tags handled by this component.
	Args:		--
	Return:	array of strings
----------------------------------------------------------------------------- */

- (NSArray *) eventTags
{
	return [NSArray arrayWithObjects: @"sync",	// kDSynchronize
												 @"SYNC",	// kDDoSynchronize
												 @"DSYN",	// kDDoSync
												 @"SYNP",	// kDDoSyncPull
												 @"SYND",	// kDDoSyncDone
												 nil ];
}


/* -----------------------------------------------------------------------------
	Determine whether we’ve synced before.
	Args:		--
	Return:	YES => the sync manager knows about us, so presumably we have synced
----------------------------------------------------------------------------- */

- (BOOL) hasSynced
{
	return [[ISyncManager sharedManager] clientWithIdentifier: kSyncClientId] != nil;
}


/* -----------------------------------------------------------------------------
	N e w t o n   D o c k   I n t e r f a c e
----------------------------------------------------------------------------- */
#pragma mark Event Handlers

/* -----------------------------------------------------------------------------
	Newton requests sync.
				kDSynchronize
	Args:		inEvent
	Return:	--
----------------------------------------------------------------------------- */

- (void) do_sync: (NCDockEvent *) inEvent
{
	isDesktopInControl = NO;
	dispatch_async(dispatch_get_main_queue(),
	^{
		[self startSyncSession];
		// after sync services prep, this will contine with DSYN event
	});
}


/* -----------------------------------------------------------------------------
	User has selected the Synchronize menu item.
				kDDoSynchronize
	Args:		inEvent
	Return:	--
----------------------------------------------------------------------------- */

- (void) do_SYNC: (NCDockEvent *) inEvent
{
	isDesktopInControl = YES;
	dispatch_async(dispatch_get_main_queue(),
	^{
		[self startSyncSession];
		// after sync services prep, this will contine with DSYN event
	});
}

/* -----------------------------------------------------------------------------
	Continue with sync.
				kDDoSync
	Args:		inEvent
	Return:	--
----------------------------------------------------------------------------- */

- (void) do_DSYN: (NCDockEvent *) inEvent
{
	if (isDesktopInControl)
		[session setDesktopControl: YES];

	[session sendEvent: kDRequestToSync expecting: kDResult];	// yes, even when Newton sends us kDSynchronize we need to request sync

	// fake -- just to get the current time
	NewtonTime syncTime = [session setLastSyncTime: 0];

	// install protocol extension
	[self installSyncExtensions];

	@try
	{
		// iterate over transformers
		txformer = nameTxformer;
		// make sure we are syncing with current data
		// iterate over stores
		for (NCStore * storeObj in dock.document.deviceObj.tetheredStores)
		{
			// iterate over soups holding the data we need to transform
			for (NSString * soupName in [txformer soupNames])
			{
				[self doBackup: storeObj soup: soupName];
			}
		}

		// push changes
		dispatch_async(dispatch_get_main_queue(),
		^{
			[self pushData];
		});
	}
	@catch (NSException * exception)
	{
		NSLog(@"-[NCSyncComponent do_DSYN:] caught %@ -- %@", [exception name], [exception reason]);
		[self cancelSync];
	}
}


/* -----------------------------------------------------------------------------
	Perform sync -- pull changes from SyncService.
				kDDoSyncPull
	Args:		inEvent
	Return:	--
----------------------------------------------------------------------------- */

- (void) do_SYNP: (NCDockEvent *) inEvent
{
	@try
	{
		// pull changes
		[self pullChangesFor: txformer entityNames: filteredEntityNames];
		[syncSession finishSyncing];
		dock.document.deviceObj.syncMode = [NSNumber numberWithInt: kFastSync];
		[session sendEvent: kDOperationDone];
		[dock synchronizeDone: syncErr];
	}
	@catch (NSException * exception)
	{
		NSLog(@"-[NCSyncComponent do_SYNP:] caught %@ -- %@", [exception name], [exception reason]);
		[self cancelSync];
	}
}


/* -----------------------------------------------------------------------------
	Perform sync -- done.
				kDDoSyncDone
	Args:		inEvent
	Return:	--
----------------------------------------------------------------------------- */

- (void) do_SYND: (NCDockEvent *) inEvent
{
//	if (syncErr)
//		[session sendEvent: kDOperationCanceled expecting: kDOpCanceledAck];
//	else
		[session sendEvent: kDOperationDone];
	[dock synchronizeDone: syncErr];
}


/* -----------------------------------------------------------------------------
	Notification from the progress slip that the user pressed the cancel button.
	Args:		inNotification
	Return:	--
----------------------------------------------------------------------------- */

- (void) cancel: (NSNotification *) inNotification
{
	[self cancelSync];
}


#pragma mark Sync
/* -----------------------------------------------------------------------------
	Register our SyncService client.
	Args:		--
	Return:	registered client
				nil => registration failed
----------------------------------------------------------------------------- */

- (ISyncClient *) registerSyncClient
{
	ISyncManager * syncManager = [ISyncManager sharedManager];
	ISyncClient * client;

	// Since we are using one of the Sync Services public schemas in this
	// application, it is not necessary to register it.
	// The contacts schema is guaranteed to be registered automatically.
	// If you are not using one of the public schemas, now is the time
	// to register it.
	//  
	// [syncManager registerSchemaWithBundlePath: CanonicalContactsSchemaPath];

	// See if our sync client has already registered...
	if (!(client = [syncManager clientWithIdentifier: kSyncClientId]))
	{
		// ...and if it hasn't, register it.
		client = [syncManager registerClientWithIdentifier: kSyncClientId
												 descriptionFilePath: [[NSBundle mainBundle] pathForResource: @"SyncClientDescription" ofType: @"plist"]];
	}

	return client;
}


/* -----------------------------------------------------------------------------
	Perform SyncService session with newton.
	First we start the SyncServices session asynchronously in the main thread.
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (void) startSyncSession
{
// in main thread...
	@try
	{
	// •	set up entity names
		nameTxformer = [[NameTransformer alloc] init];
#if 0
		dateTxformer = [[DateTransformer alloc] init];
		entityNames = [[nameTxformer entityNames] arrayByAddingObjectsFromArray: [dateTxformer entityNames]];
#else
		entityNames = [nameTxformer entityNames];
#endif
		filteredEntityNames = nil;

		syncErr = noErr;
		syncSession = nil;
	//	•	get sync client
		ISyncClient * synClient = [self registerSyncClient];
		if (synClient == nil)
			@throw [NSException exceptionWithName: @"NoSyncClient" reason: @"Can’t register sync client." userInfo: nil];
/*
		// If you are going to be doing record filtering,
		// set the filters on the client before starting the session.
		if (m_syncsUsingRecordFiltering)
		{
			id filter = [LastNameFilter filter];
			[synClient setFilters: [NSArray arrayWithObject: filter]]; 
		}
		else
		{
			[synClient setFilters: [NSArray array]]; 
		}
*/
		// If you are pulling the truth,
		// tell the client this fact before starting the session.
		if ([dock.document.deviceObj.syncMode intValue] == kPullTheTruth)
			[synClient setShouldReplaceClientRecords: YES forEntityNames: entityNames];

	//	•	create sync session with SyncService
		// Ask for a session to be started in the background.
		// This is a good choice if starting the session from the main thread,
		// since other clients in other processes may be joining,
		// and you will not want to block your UI while that handshaking is taking place.
		[ISyncSession beginSessionInBackgroundWithClient: synClient
														 entityNames: entityNames 
																target: self
															 selector: @selector(performSync:session:)];
	}

	@catch(NSException * exception)
	{
		NSLog(@"-[NCSyncComponent startSyncSession] caught %@ -- %@", [exception name], [exception  reason]);
		dock.document.deviceObj.syncMode = [NSNumber numberWithInt: kSlowSync];
		[dock synchronizeDone: -1];	// should think of something better here
	}
}


/* -----------------------------------------------------------------------------
	Call back after creating sync session. Go push new/changed/deleted records.
	Args:		inSyncClient
				inSyncSession		nil => sync was not started
	Return:	--
----------------------------------------------------------------------------- */

- (void) performSync: (ISyncClient *) inSyncClient session: (ISyncSession *) inSyncSession
{
// in main thread...
	@try
	{
		syncClient = inSyncClient;
		syncSession = inSyncSession;
		if (syncSession)
		{
			[self negotiateSession];
			[session doEvent: kDDoSync];
		}
		else
		{
			[dock synchronizeDone: noErr];	// maybe should explain the situation to the user
		}
	}
	@catch (NSException * exception)
	{
		NSLog(@"-[NCSyncComponent performSync:session:] caught %@ -- %@", [exception name], [exception reason]);
	//	syncErr = ?;
		[self cancelSync];
	}
}


/* -----------------------------------------------------------------------------
	This method helps to negotiate a sync mode for the session.
	It’s pretty simple in this example, but in the case of a "real" app,
	setting the sync mode would probably take some input from application state.
	For instance, if an application has lost part of its data set, it will likely
	wish to perform a refresh sync.
	It is also important to note that this is a "negotiation" process. You ask
	for the mode you want here, but the kind of session you get is really up
	to the sync engine. Later, in the push and pull phases, we will find out
	what the sync engine has decided.

	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (void) negotiateSession
{
	switch ([dock.document.deviceObj.syncMode intValue])
	{
	case kFastSync:
		// nothing to do here.
		break;
	case kSlowSync:
		[syncSession clientWantsToPushAllRecordsForEntityNames: entityNames];
		break;
	case kRefreshSync:
		[syncSession clientDidResetEntityNames: entityNames];
		break;
	case kPullTheTruth:
		// not handled here. must be handled before syncSession starts.
		break;
	}
}


#pragma mark -- Push
/* -----------------------------------------------------------------------------
 sync
  push data
   iterate over entity names
	 iterate over stores
	  iterate over soups
	   we need time and ids from previous sync
		if shouldPushAllRecordsForEntityName
		 fetch all persistent entries in this soup
		  build id list -- just query the soup
		  transform for SyncServices
		  push
		  how does user cancel this loop?
		 else if shouldPushChangesForEntityName
		  fetch persistent entries w/ mod date or id > prev
		  treat as above
		calc delta with prev ids
		push deletes of ids in the delta
		prev ids <- ids from current sync
  pull data
   build required entity names
   prepareToPullChangesForEntityNames
	 mingle
	iterate over changes
	 add | modify | delete
	
	finishSyncing


	NCSoup holds sync state in a pair of ivars:
		prevSynchTime:	NSDate*		Newton time of sync	so we can fetch changed entries
		prevSynchIds:	NSIndexSet*	ids of all entries	so we can fetch new and deleted entries


	iterate over entity names --uh, set txformer
fetch:		in thread
		iterate over stores
			create sync file for this entity-store eg “Names-internal.nsof”, “Names-card1.nsof”
				  and id file for this entity-store eg “Names-internal-ids.nsof”, “Names-card1-ids.nsof”
			set soup
			if SyncService wants all records
				ask newt to send whole soup
				foreach entry
					if xformer wants this class of entry
						flatten it to file
						collect its id
			else (fast syncing)
				ask newt to backup soup
				foreach entry
					if xformer wants this class of entry
						flatten it to file
						collect its id
				collect ids of unchanged entries

push:			in main --uh, we still need to say HELO?
		iterate over stores
			read sync file for this entity-store and read entries from it
				transform entry -> entity
				push
			diff prev ids | current ids to get deleted ids
				read NSData * prevIds for this store
				create CPtrPipe from (char *) (NSData *) and read array object from it
				read id file for this entity-store and read array object from it
				FSetDifference
			foreach id in the delta push (deleted id)

mingle:

pull:

send:
		iterate over stores
			set soup
			read sync file for this entity-store and read entries from it
				send each entry

finish:
		finish with sync services
		iterate over stores
			save info in soup slot
				store id file for this store in syncClient (read it into NSData *)
				also time of this successful sync

----------------------------------------------------------------------------- */


/* -----------------------------------------------------------------------------
	Perform SyncService session with Newton.
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (void) pushData
{
// in main thread...
	newton_try
	{
	// •	iterate over entity names (transformers)
		txformer = nameTxformer;

		unsigned int index = 0;
		NSString * entity = [txformer mainEntityName];

		// iterate over all tethered stores
		for (NCStore * storeObj in dock.document.deviceObj.tetheredStores)
		{
			[txformer setStore: storeObj.name];	// storeId

			// iterate over soups holding the data we need to transform
			for (NSString * soupName in [txformer soupNames])
			{
				dock.document.progress.title = storeObj.name;
				dock.document.progress.status = [NSString stringWithFormat: NSLocalizedString(@"pushing", nil), soupName];

				NCSoup * soupObj = [storeObj findSoup:soupName];

				NSIndexSet * prevIds = nil;
				NSArray * entries = nil;
				BOOL isFastSync = NO;

				if ([syncSession shouldPushAllRecordsForEntityName: [txformer mainEntityName]])
				{
					// -----------------------------
					// Slow sync. Push all records we have.

					entries = [soupObj orderedEntries];
				}

				else if ([syncSession shouldPushChangesForEntityName: [txformer mainEntityName]])
				{
					// -----------------------------
					// Fast sync.

					prevIds = soupObj.prevSynchIds;
NSLog(@"Fetching entries with date > %@ or id > %d", soupObj.prevSynchTime, [prevIds lastIndex]);
					entries = [soupObj entriesLaterThan:soupObj.prevSynchTime withIdGreaterThan:[prevIds lastIndex]];
					isFastSync = YES;
				}

				if (entries)
				{
					for (NCEntry * entry in entries)
					{
						CPtrPipe pipe;
						pipe.init((void *)entry.refData.bytes, entry.refData.length, NO, nil);
						NSDictionary * syncRecord = [txformer transformEntry: UnflattenRef(pipe)];
						if (syncRecord != nil)
						{
							for (NSString * uid in [syncRecord allKeys])
							{
NSLog(@"pushing record id %@", uid);
								[syncSession pushChangesFromRecord: [syncRecord objectForKey: uid] withIdentifier: uid];
							}
						}
						if (progress.isCancelled)
							ThrowErr(exStore, kNCErrOperationCancelled);
					}
				}

				if (isFastSync)
				{
					// if we pushed changes, we need to push deletes too
					// calc delta with prev ids
					// push deletes of ids in the delta
					// prev ids <- ids from current sync
					if (prevIds && prevIds.count > 0)
					{
						NSIndexSet * currIds = soupObj.currIds;
						NSUInteger uid;
						for (uid = [prevIds firstIndex]; uid != NSNotFound; uid = [prevIds indexGreaterThanIndex:uid])
						{
							if (![currIds containsIndex:uid])
							{
NSLog(@"Deleting %@", uid);
								[syncSession deleteRecordWithIdentifier: [txformer entityIdFor: uid]];
								// need to delete all subentities when deleting a main entity?
								// maybe prevIds should be an array of arrays, each subarray holding all the entity ids for an entry
							}
						}
					}
				}
			}	// foreach soup
		}	// foreach store
	}
	newton_catch_all
	{
		syncErr = (NewtonErr)(long)CurrentException()->data;
NSLog(@"-[NCSyncComponent pushData] raised Newton exception %s (%d)", CurrentException()->name, syncErr);
	}
	end_try;

	if (YES || syncErr)	// DEBUG
	{
		[self cancelSync];
	}

	else
	{
		/* -----------------------------------------------------------------------------
			Set up Sync Services to pull data. Mingling is done in the background.
		----------------------------------------------------------------------------- */
		@try
		{
			//	•	SyncService may not want us to pull some entities
			filteredEntityNames = [[NSMutableArray alloc] initWithCapacity: [entityNames count]];
			for (NSString * entityName in entityNames)
			{
				if ([syncSession shouldPullChangesForEntityName: entityName])
					[filteredEntityNames addObject: entityName];
else NSLog(@"SyncService does not want to give us any changes for %@", entityName);
			}

			//	•	let SyncService mingle…
			dock.document.progress.title = NSLocalizedString(@"sync service", nil);
			dock.document.progress.status = NSLocalizedString(@"mingling", nil);
			dock.document.progress.value = 50.0;

			[syncSession prepareToPullChangesInBackgroundForEntityNames:filteredEntityNames target:self selector:@selector(performPull:session:)];
			// accept cancellation from UI? [syncSession cancelSyncing] would do it
		}
		@catch(NSException * exception)
		{
			NSLog(@"-[NCSyncComponent pullData] caught %@ -- %@", [exception name], [exception  reason]);
			[self cancelSync];
		}
	}
}


/* -----------------------------------------------------------------------------
	Back up soup so we will sync with current data.
	Copied from BackupComponent.mm -- maybe one day there could be a common
	function.
	Args:		inStoreObj
				inSoupName
	Return:	--
----------------------------------------------------------------------------- */

- (void) doBackup: (NCStore *) inStoreObj soup: (NSString *) inSoupName
{
	newton_try
	{
		// set current store
		[session setCurrentStore: inStoreObj.ref info: NO];

		// get matching arrays of soupName, soupSignature for all soups on this store
		RefVar allSoups([session getAllSoups]);
		RefVar soupNames(GetArraySlot(allSoups, 0));
		RefVar soupSignatures(GetArraySlot(allSoups, 1));

// and this is where our story really starts…

		if ([session setCurrentSoup: MakeString(inSoupName)] == noErr)
		{
			NSString * storeObjName = inStoreObj.name;
			dispatch_async(dispatch_get_main_queue(),
			^{
				dock.document.progress.title = storeObjName;
				dock.document.progress.status = [NSString stringWithFormat:@"Backing up %@ for sync", inSoupName];
				dock.document.progress.value = 1.0;
			});

			// look up soup’s signature from its name
			int soupSignature = 0;
			Ref sigIndex;
			if (ISINT(sigIndex = FFindStringInArray(RA(NILREF), soupNames, MakeString(inSoupName))))
				soupSignature = RVALUE(GetArraySlot(soupSignatures, RVALUE(sigIndex)));

			RefVar soupInfo, soupIndex;
			NewtonTime syncTime = 0;
			EventType result = 0;

			NCDockEvent * evt;
			NCSoup * soupObj;
			if (soupObj = [inStoreObj findSoup:inSoupName])
			{
				// soup exists -- update it
				syncTime = [session setLastSyncTime: soupObj.lastSyncTime];
				XTRY
				{
					// if soup info has changed, update it
					evt = [session sendEvent: kDGetChangedInfo expecting: kDAnyEvent];
					XFAILIF(evt.tag == kDOperationCanceled, result = kDOperationCanceled;)	// user cancelled
					if (evt.tag == kDSoupInfo)
					{
						soupInfo = evt.ref;
						[soupObj updateInfo:soupInfo];
					}

					// if soup index has changed, update it
					evt = [session sendEvent: kDGetChangedIndex expecting: kDAnyEvent];
					XFAILIF(evt.tag == kDOperationCanceled, result = kDOperationCanceled;)	// user cancelled
					if (evt.tag == kDIndexDescription)
					{
						soupIndex = evt.ref;
						[soupObj updateIndex:soupIndex];
					}

					soupObj.signature = [NSNumber numberWithLong:soupSignature];

					NCIdList * idList = [[NCIdList alloc] init];

					[session sendEvent: kDBackupSoup value: [soupObj.lastBackupId unsignedIntValue]];
					for (;;)
					{
						evt = [session receiveEvent: kDAnyEvent];
						// expecting kDSoupNotDirty, kDEntry, kDSetBaseID, kDBackupIDs, kDBackupSoupDone, kDOperationCanceled
						if (evt.tag == kDEntry)
						{
							NCEntry * entryObj = [soupObj addEntry:evt.ref withNSOFData:evt.data length:evt.dataLength];
							[idList addId:[entryObj.uniqueId unsignedIntValue]];
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
							[session sendEvent: kDOpCanceledAck];
							result = evt.tag;
							break;
						}
						if (progress.isCancelled)
						{
							[session sendEvent:kDOperationCanceled /*expecting:kDOpCanceledAck*/];
							result = kDOperationCanceled;
							break;
						}
					}

					if (result == kDBackupSoupDone)
					{
						// calc delta with prev ids
						// delete entries with ids in the delta
						[soupObj cropTo:idList.ids];
					}
				}
				XENDTRY;
			}
			else
			{
				// we don’t know about this soup yet
				syncTime = [session setLastSyncTime: 0];
				XTRY
				{
					evt = [session sendEvent: kDGetSoupInfo expecting: kDAnyEvent];
					XFAILIF(evt.tag == kDOperationCanceled, result = kDOperationCanceled;)	// user cancelled
					if (evt.tag == kDSoupInfo)
						soupInfo = evt.ref;

					evt = [session sendEvent: kDGetIndexDescription expecting: kDAnyEvent];
					XFAILIF(evt.tag == kDOperationCanceled, result = kDOperationCanceled;)	// user cancelled
					if (evt.tag == kDIndexDescription)
						soupIndex = evt.ref;

					soupObj = [inStoreObj addSoup:inSoupName signature:soupSignature indexes:soupIndex info:soupInfo];
// we don’t know what app this soup belongs to -- but does this really matter?
// won’t it be sorted out when we do a bona fide backup?
//					[document app:appObj addSoup:soupObj];
// -- update UI sidebar APPS w/ new soup

					[session sendEvent: kDSendSoup];
					for (;;)
					{
						evt = [session receiveEvent: kDAnyEvent];
						// expecting kDEntry, kDBackupSoupDone, kDOperationCanceled
						if (evt.tag == kDEntry)
						{
							[soupObj addEntry:evt.ref withNSOFData:evt.data length:evt.dataLength];
						}
						else if (evt.tag == kDBackupSoupDone)
						{
							result = evt.tag;
							break;
						}
						else if (evt.tag == kDOperationCanceled)
						{
							[session sendEvent: kDOpCanceledAck];
							result = evt.tag;
							break;
						}
						if (progress.isCancelled)
						{
							[session sendEvent:kDOperationCanceled /*expecting:kDOpCanceledAck*/];
							result = kDOperationCanceled;
							break;
						}
					}
				}
				XENDTRY;
			}

			// good time to save the document?
			[dock.document savePersistentStore];

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
				SetFrameSlot(soupInfo, MakeSymbol("NCKLastBackupTime"), MAKEINT(syncTime));
				[soupObj updateInfo: soupInfo];
				[session setSoupInfo: soupInfo];
			}
		}
else REPprintf("\n#### soup not on store!");
	}
	newton_catch_all
	{
		syncErr = (NewtonErr)(long)CurrentException()->data;
		REPprintf("\n-[NCSyncComponent doBackup:soup:] raised Newton exception %s (%d)\n", CurrentException()->name, syncErr);
	}
	end_try;
}


#pragma mark -- Pull
/* -----------------------------------------------------------------------------
	Call back after mingling. Pull new/changed/deleted records.
	All we need do here is switch back to protocol thread.
	Args:		inSyncClient
				inSyncSession		nil => sync was cancelled
	Return:	--
----------------------------------------------------------------------------- */

- (void) performPull: (ISyncClient *) inSyncClient session: (ISyncSession *) inSyncSession
{
// in main thread...
	@try
	{
		syncClient = inSyncClient;
		syncSession = inSyncSession;
		if (syncSession)
		{
			[session doEvent: kDDoSyncPull];
/*			'->
			// pull changes into CoreData
			[self pullChangesFor: txformer entityNames: filteredEntityNames];
			[syncSession finishSyncing];
			dock.document.deviceObj.syncMode = [NSNumber numberWithInt: kFastSync];

			// sync again w/ Newton
			//[self doBackup]; ??
			[session sendEvent: kDOperationDone];
			[dock synchronizeDone: syncErr];
*/		}
		else
		{
			[dock synchronizeDone: kNCErrNoSyncSession];
		}
	}
	@catch (NSException * exception)
	{
		NSLog(@"-performPull:session: caught %@ -- %@", [exception name], [exception reason]);
		[self cancelSync];
	}
}


/* -----------------------------------------------------------------------------
	Pull changes from SyncService and send to Newton.
	Pass 1:
		iterate over changes
			if main entity
				add => add to default store/newEntities dictionary
				modify => add entry to store/modifiedEntities by store id
				delete => add entry id to store/deletedEntities by store id
			else
				add => add subEntity
				modify => add subEntity • if main entity doesn’t exist, fetch it from newt?
				delete => ignore it, main entry will have been modified/deleted anyway -- but accept it!
	Pass 2:
		iterate over stores
			empty soup if necessary
			iterate over entries in modifiedEntities
				[session changeEntry: transformedEntry]
			iterate over entries in deletedEntities
				[session deleteEntryID: entryId]
		use default store
			iterate over entries in newEntities
				[session addEntry: transformedEntry]
	Args:		inTransformer
				inEntityNames		not necessarily all of them
	Return:	--
----------------------------------------------------------------------------- */

- (void) pullChangesFor: (EntityTransformer *) inTransformer entityNames: (NSArray *) inEntityNames
{
#if 1
	NSMutableDictionary * newRecords;
	NSMutableDictionary * modifiedRecords;
	NSMutableDictionary * subEntities;

	NSString * recordId;
	NSDictionary * record;
	NSDictionary * deletedValue = [NSDictionary dictionaryWithObject:[NSNumber numberWithBool:YES] forKey:@"deleted"];

	NSString * mainEntityName = [inTransformer mainEntityName];
	BOOL isMainEntity;

	if ([syncSession shouldReplaceAllRecordsOnClientForEntityName: mainEntityName])
	{
		for (NCStore * storeObj in dock.document.deviceObj.stores)	// that’s ALL stores, not just tethered
		{
			// iterate over soups holding the data we need to transform
			for (NSString * soupName in [inTransformer soupNames])
			{
				NCSoup * soupObj = [storeObj findSoup:soupName];
NSLog(@"Emptying %@ %@ soup", storeObj.name,soupObj.name);
				// NOTE that if you empty the Names soup you will lose owner cards, worksites and groups.
				// so we MUST push owner cards, worksites and groups

				//[soupObj empty]; --clear all entries--
				soupObj.prevSynchIds = [NSMutableIndexSet indexSet];
			}
		}
	}

/* -------------------------------------------------------------
	Pass 1 -- build dictionaries of new & modified records.
------------------------------------------------------------- */
	dispatch_async(dispatch_get_main_queue(),
	^{
		dock.document.progress.status = [NSString stringWithFormat: NSLocalizedString(@"pulling", nil), [[inTransformer soupNames] objectAtIndex: 0]];
		dock.document.progress.value = 75.0;
	});

	NSEnumerator * changeIter = [syncSession changeEnumeratorForEntityNames: inEntityNames];
	while (ISyncChange * change = [changeIter nextObject])
	{
		recordId = [change recordIdentifier];
		record = [change record];
		isMainEntity = [inTransformer isMainEntity: record];
		switch ([change type])
		{
		case ISyncChangeTypeAdd:
			if (isMainEntity)
				[newRecords setValue: record forKey: recordId];
			else
				[subEntities setValue: record forKey: recordId];
			break;

		case ISyncChangeTypeModify:
			if (isMainEntity)
				[modifiedRecords setValue: record forKey: recordId];
			else
				[subEntities setValue: record forKey: recordId];
			break;

		case ISyncChangeTypeDelete:
			if (isMainEntity)
				[modifiedRecords setValue: deletedValue forKey: recordId];
			else
				[syncSession clientAcceptedChangesForRecordWithIdentifier: recordId
																		formattedRecord: nil
																  newRecordIdentifier: nil];
			break;
		}
	}

/* -------------------------------------------------------------
	Pass 2 -- iterate over modified records, update soup entries.
------------------------------------------------------------- */
	NCStore * storeObj;
	NCSoup * soupObj;

	for (recordId in modifiedRecords)
	{
		// modified records MUST have an id in our schema
		// so we can establish which store/soup the record belongs to
		NSString * storeName = nil;
		NSString * soupName = nil;
		NSString * entryId = nil;
		NSArray * idParts = [recordId componentsSeparatedByString: @"-"];
		if ([idParts count] >= 3)
		{
			storeName = [idParts objectAtIndex: 0];
			soupName = [idParts objectAtIndex: 1];
			entryId = [idParts objectAtIndex: 2];
		}
		if (entryId == nil)
		{
NSLog(@"#### No _uniqueId in record id %@ ####", recordId);
			continue;
		}
		storeObj = [dock.document findStoreNamed:storeName];
		soupObj = [storeObj findSoup:soupName];

		record = [modifiedRecords objectForKey:recordId];
		if (record == deletedValue)
		{
NSLog(@"Deleting %@", recordId);
			unsigned int uid = [entryIdStr unsignedIntValue];
			[soupObj deleteEntryId:uid];
		}
		else
		{
NSLog(@"Modifying %@", recordId);
			;
		}
		[syncSession clientAcceptedChangesForRecordWithIdentifier: recordId
																formattedRecord: nil
														  newRecordIdentifier: nil];
	}

/* -------------------------------------------------------------
	Pass 3 -- iterate over new records, add soup entries.
------------------------------------------------------------- */

//	soupObj = Names soup on default store
	storeObj = [dock.document defaultStore];
	soupObj = [storeObj findSoup:soupName];
	for (recordId in newRecords)
	{
#if kDebugOn
NSLog(@"Adding %@", recordId);
#endif
		record = [newRecords objectForKey:recordId];
		RefVar entryRef([inTransformer transformEntry:NILREF with:record subEntities:subEntities]);
		[soupObj addEntry:entryRef];
		NSArray * idMap = [inTransformer entityIdMap: recordId forEntryId: uid];
		unsigned i, count = [idMap count];
		for (i = 0; i < count; i += 2)
		{
			[syncSession clientAcceptedChangesForRecordWithIdentifier: [idMap objectAtIndex: i]
																	formattedRecord: nil
															  newRecordIdentifier: [idMap objectAtIndex: i+1]];
		}
		else
			NSLog(@"Failed to add record id %@", recordId);
	}


#else
	NSMutableDictionary * subEntityDict;
	NSMutableDictionary * entityDict;
	NSString * storeId, * defaultStoreId = nil;
	NSMutableDictionary * stores;
	NSMutableArray * storeItems;
	NSMutableArray * idArray;
	NSString * recordId;
	NSDictionary * record;
	NSEnumerator * recordIter;
	NSEnumerator * changeIter;
	ISyncChange * change;
	NSString * keyPath;
	unsigned index;

	NSString * mainEntityName = [inTransformer mainEntityName];

	if ([inEntityNames containsObject: mainEntityName])
	{
		NSArray * tetheredStores = dock.document.deviceObj.tetheredStores;
		// create stores dictionary, containing new/modified/deleted records per store
		// key path of the form storeId . new . entities . recordId
		index = 0;
		stores = [NSMutableDictionary dictionaryWithCapacity: tetheredStores.count];
		for (NCStore * storeObj in tetheredStores)
		{
			NSDictionary * newItems = [NSDictionary dictionaryWithObjectsAndKeys:
										[NSMutableDictionary dictionary], @"entities",
										[NSMutableDictionary dictionary], @"subEntities",
										nil];
			NSDictionary * modifiedItems = [NSDictionary dictionaryWithObjectsAndKeys:
										[NSMutableDictionary dictionary], @"entities",
										[NSMutableDictionary dictionary], @"subEntities",
										nil];
			NSDictionary * deletedItems = [NSDictionary dictionaryWithObjectsAndKeys:
										[NSMutableArray array], @"entities",
										[NSMutableArray array], @"subEntities",
										nil];
			storeItems = [NSDictionary dictionaryWithObjectsAndKeys:
										newItems, @"new",
										modifiedItems, @"modified",
										deletedItems, @"deleted",
										nil];

			[stores setObject: storeItems forKey: storeObj.name];	//MakeStoreId(index++);
			if (index++ == 0)
				defaultStoreId = storeObj.name;
			if (storeObj.isDefault)
				defaultStoreId = storeObj.name;
		}

// -----------------------------
// Pass 1
		dispatch_async(dispatch_get_main_queue(),
		^{
			dock.document.progress.status = [NSString stringWithFormat: NSLocalizedString(@"pulling", nil), [[inTransformer soupNames] objectAtIndex: 0]];
			dock.document.progress.value = 75.0;
		});
	//	iterate over changes and allocate to appropriate dictionary by store
		changeIter = [syncSession changeEnumeratorForEntityNames: [inTransformer entityNames]];
		while (change = [changeIter nextObject])
		{
			recordId = [change recordIdentifier];
			switch ([change type])
			{
			case ISyncChangeTypeAdd:
				record = [change record];
				keyPath = [NSString stringWithFormat:@"%@.new.%@.%@", defaultStoreId, [inTransformer isMainEntity: record] ? @"entities" : @"subEntities", recordId];
				[stores setValue: record forKeyPath: keyPath];
				break;

			case ISyncChangeTypeModify:
				// find dict for right store
				storeId = StoreIdFrom(recordId);
				if (storeId == nil)
					break;	// ??
				keyPath = [NSString stringWithFormat:@"%@.modified.%@.%@", storeId, IsMainEntityId(recordId) ? @"entities" : @"subEntities", recordId];
				[stores setValue: [change record] forKeyPath: keyPath];
				break;

			case ISyncChangeTypeDelete:
				// find array for right store
				storeId = StoreIdFrom(recordId);
				if (storeId == nil)
					break;	// ??
				keyPath = [NSString stringWithFormat:@"%@.deleted.%@", storeId, IsMainEntityId(recordId) ? @"entities" : @"subEntities"];
				idArray = [stores valueForKeyPath: keyPath];
				[idArray addObject: recordId];
				break;
			}
		}
#if kDebugOn
NSLog(@"Received changes: %@", stores);
#endif

// -----------------------------
// Pass 2
		unsigned int entryId;
		NSString * entryIdStr;
		newton_try
		{
			index = 0;
			for (NCStore * storeObj in tetheredStores)
			{
				NSString * storeObjName = storeObj.name;
				storeId = storeObj.name;	//MakeStoreId(index++);
				storeItems = [stores objectForKey: storeId];

				// get the state of this store at last sync

				dispatch_async(dispatch_get_main_queue(),
				^{
					dock.document.progress.title = storeObjName;
					dock.document.progress.status = nil;
				});
				[session setCurrentStore: storeObj.ref info: NO];
				[inTransformer setStore: storeObj.name];	// storeId

				for (NSString * soupName in [inTransformer soupNames])
				{
					NCSoup * soupObj = [storeObj findSoup:soupName];	// cannot be nil b/c we just backed it up

					dispatch_async(dispatch_get_main_queue(),
					^{
						dock.document.progress.status = [NSString stringWithFormat: NSLocalizedString(@"pulling", nil), soupName];
					});
					if ([session setCurrentSoup: MakeString(soupName)] != noErr)
						ThrowMsg("soup does not exist");
// need to find a way to iterate over entities for this soup

					// empty soup if Sync Service wants to replace all records
					if ([syncSession shouldReplaceAllRecordsOnClientForEntityName: mainEntityName])
					{
NSLog(@"Emptying %@ soup", soupName);
					//	[session emptySoup];
					// NOTE that if you empty the Names soup you will lose owner cards, worksites and groups.

					// should be an -emptySoup: (NCSession *) inSession method on EntityTransformer?
					// could save owner cards, worksites and groups; emptySoup; restore owner cards, worksites and groups
						RefVar argFrame(AllocateFrame());
						SetFrameSlot(argFrame, SYMA(signature), MAKEINT([storeObj.signature longValue]));
						[session callExtension: 'eNsp' with: argFrame];

						soupObj.prevSynchIds = [NSMutableIndexSet indexSet];
					}
					else
					{
					// delete specified entries from this store
						idArray = [stores valueForKeyPath: [NSString stringWithFormat:@"%@.deleted.entities", storeId]];
						for (recordId in idArray)
						{
NSLog(@"Deleting %@", recordId);
							entryIdStr = EntryIdFrom(recordId);
							if (entryIdStr)
							{
								unsigned int uid = MAKEINT([entryIdStr intValue]);
								[session deleteEntryId:uid];
								[soupObj deleteEntryId:uid];
							}
							[syncSession clientAcceptedChangesForRecordWithIdentifier: recordId
																					formattedRecord: nil
																			  newRecordIdentifier: nil];
						}
					}

					// accept deleted subentities from this store -- we’re never going to refer to them
					idArray = [stores valueForKeyPath: [NSString stringWithFormat:@"%@.deleted.subEntities", storeId]];
					for (recordId in idArray)
					{
NSLog(@"Deleting %@", recordId);
						[syncSession clientAcceptedChangesForRecordWithIdentifier: recordId
																				formattedRecord: nil
																		  newRecordIdentifier: nil];
					}

					// if any modified subEntities don’t have a parent in our modified entities dictionary,
					// create parent entries for them
					entityDict = [stores valueForKeyPath: [NSString stringWithFormat:@"%@.modified.entities", storeId]];
					subEntityDict = [stores valueForKeyPath: [NSString stringWithFormat:@"%@.modified.subEntities", storeId]];
					if ([subEntityDict count] > 0)
					{
						NSMutableArray * parentIds = [NSMutableArray arrayWithCapacity: 4];
						recordIter = [subEntityDict keyEnumerator];
						while (recordId = [recordIter nextObject])
						{
							entryIdStr = MainEntityIdFrom(recordId);
							if (entryIdStr
							&&  [entityDict objectForKey: entryIdStr] == nil
							&&  ![parentIds containsObject: entryIdStr])
							{
								[parentIds addObject: entryIdStr];
							}
						}
						if ([parentIds count] > 0)
						{
							ISyncRecordSnapshot * snapshot = [syncSession snapshotOfRecordsInTruth];
							NSDictionary * records = [snapshot recordsWithIdentifiers: parentIds];
							[entityDict addEntriesFromDictionary: records];
						}
					}

					// modify entries on this store
					if ([entityDict count] > 0)
					{
						NSMutableDictionary * allSubEntities = [NSMutableDictionary dictionaryWithDictionary: [stores valueForKeyPath: [NSString stringWithFormat:@"%@.new.subEntities", defaultStoreId]]];
						[allSubEntities addEntriesFromDictionary: [stores valueForKeyPath: [NSString stringWithFormat:@"%@.modified.subEntities", defaultStoreId]]];
						recordIter = [entityDict keyEnumerator];
						while (recordId = [recordIter nextObject])
						{
NSLog(@"Modifying %@", recordId);
							RefVar entryRef;
							unsigned int uid = 0;
							entryIdStr = EntryIdFrom(recordId);
							if (entryIdStr)
								uid = [entryIdStr intValue];
							if (uid != 0)
								entryRef = [soupObj entryWithId: uid];
							if (NOTNIL(entryRef))
							{
#if kDebugOn
REPprintf("\nModifying existing entry:\n");
PrintObject(entryRef, 0);
#endif
								record = [entityDict objectForKey: recordId];
#if kDebugOn
NSLog(@"\nwith:\n%@", record);
#endif
								entryRef = [inTransformer transformEntry: entryRef with: record subEntities: allSubEntities];
#if kDebugOn
REPprintf("\nto:\n");
PrintObject(entryRef, 0);
#endif
							// change the entry we just read
								if (NOTNIL(entryRef)
								&&  [session changeEntry: entryRef] == noErr)
								{
									[soupObj addEntry:entryRef];	// will actually update
									NSArray * idMap = [inTransformer entityIdMap: recordId forEntryId: uid];
									unsigned i, count = [idMap count];
									for (i = 0; i < count; i += 2)
									{
										NSString * fromId = [idMap objectAtIndex: i];
										NSString * toId = [idMap objectAtIndex: i+1];
										// accept changes, mapping ids as we go
										if ([entityDict objectForKey: fromId] != nil
										||  [subEntityDict objectForKey: fromId] != nil)
											[syncSession clientAcceptedChangesForRecordWithIdentifier: fromId
																									formattedRecord: nil
																							  newRecordIdentifier: [fromId isInDomain: toId] ? nil : toId];
									}
								}
							}
							else
								NSLog(@"Failed to modify record id %@", recordId);
						}
					}

					// add new entries to default store
					if ((entityDict = [stores valueForKeyPath: [NSString stringWithFormat:@"%@.new.entities", storeId]]) != nil
					&&  [entityDict count] > 0)
					{
						subEntityDict = [stores valueForKeyPath: [NSString stringWithFormat:@"%@.new.subEntities", storeId]];
						recordIter = [entityDict keyEnumerator];
						while (recordId = [recordIter nextObject])
						{
#if kDebugOn
NSLog(@"Adding %@", recordId);
#endif
							record = [entityDict objectForKey: recordId];
							RefVar entryRef([inTransformer transformEntry: NILREF with: record subEntities: subEntityDict]);
							unsigned int uid = [session addEntry: entryRef];
							if (uid != 0)
							{
								[soupObj addEntry:entryRef];
								NSArray * idMap = [inTransformer entityIdMap: recordId forEntryId: uid];
								unsigned i, count = [idMap count];
								for (i = 0; i < count; i += 2)
									[syncSession clientAcceptedChangesForRecordWithIdentifier: [idMap objectAtIndex: i]
																							formattedRecord: nil
																					  newRecordIdentifier: [idMap objectAtIndex: i+1]];
							}
							else
								NSLog(@"Failed to add record id %@", recordId);
						}
					}

					// all done!
					// update what we know about this soup: ids, time synced
					NewtonTime syncTime = [session setLastSyncTime: 0];
					soupObj.prevSynchIds = soupObj.currIds;
					soupObj.prevSynchTime = [NSDate date];

				}	// foreach soup
			}
			[syncSession clientCommittedAcceptedChanges];
		}
		newton_catch_all
		{
			syncErr = (NewtonErr)(long)CurrentException()->data;
NSLog(@"Newton exception %s (%d) during pullChangesFor:stores:entityNames:", CurrentException()->name, syncErr);
		}	// raise Obj-C exception?
		end_try;
	}
#endif
}


/* -----------------------------------------------------------------------------
	Clean up after SyncService session.
	Assume we’re in a dock protocol command thread.
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (void) cancelSync
{
	if (syncSession)
		[syncSession cancelSyncing];
	dock.document.deviceObj.syncMode = [NSNumber numberWithInt: kSlowSync];
	if (syncErr == noErr) syncErr = kNCErrOperationCancelled;	// should think of something better here
	[session doEvent: kDDoSyncDone];
}

@end
