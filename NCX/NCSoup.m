/*
	File:		NCSoup.m

	Abstract:	An NCSoup models a Newton soup.
					It contains info about the soup, and its entries.

	Written by:		Newton Research, 2012.
*/

#import "NCSoup.h"
#import "Logging.h"

extern int	REPprintf(const char * inFormat, ...);


@implementation NCSoup
/* -----------------------------------------------------------------------------
	P r o p e r t i e s
----------------------------------------------------------------------------- */

- (NSString *) identifier {
	return @"Soup";
}

@dynamic name;
@dynamic signature;
@dynamic indexes;
@dynamic info;
@dynamic descr;
@dynamic appName;
@dynamic lastBackupTime;
@dynamic lastBackupId;
@dynamic lastImportId;
@dynamic prevSynchTime;
@dynamic prevSynchIdData;
@dynamic entries;
@dynamic app;
@dynamic store;

@synthesize columnInfo;

- (NSImage *) image {
	return [NSImage imageNamed: @"source-soup.png"];
}

- (NewtonTime) lastSyncTime
{
	return [self.lastBackupTime unsignedIntValue];
}

//@property(nonatomic,retain) -- will need to encode/decode to NSData
- (void) setPrevSynchIds: (NSIndexSet *) inIds
{
	self.prevSynchIdData = [NSKeyedArchiver archivedDataWithRootObject:inIds];
}

- (NSIndexSet *) prevSynchIds
{
	NSData * idData = self.prevSynchIdData;
	if (idData == nil || idData.length == 0)
		return [NSIndexSet indexSet];
	return [NSKeyedUnarchiver unarchiveObjectWithData:idData];
}

//@property (readonly) -- query the soup’s entries, build NSIndexSet
- (NSIndexSet *) currIds
{
	NSMutableIndexSet * indexSet = [NSMutableIndexSet indexSet];
	for (NCEntry * entry in self.entries)	// previously used [self orderedEntries]
	{
		unsigned int uid = [entry.uniqueId unsignedIntValue];
		if (uid < kImportIdBase)
			[indexSet addIndex:uid];
	}
	return [[NSIndexSet alloc] initWithIndexSet:indexSet];
}


/* -----------------------------------------------------------------------------
	Return all entries in the soup, ordered by _uniqueId.
	Args:		--
	Return:	NSArray *
----------------------------------------------------------------------------- */

- (NSArray *) orderedEntries
{
	NSManagedObjectContext * objContext = [self managedObjectContext];
	NSFetchRequest * request = [[NSFetchRequest alloc] init];
	[request setEntity:[NSEntityDescription entityForName:@"Entry" inManagedObjectContext:objContext]];
	[request setPredicate:[NSPredicate predicateWithFormat:@"soup = %@", self]];

	NSSortDescriptor * sorter = [[NSSortDescriptor alloc] initWithKey:@"uniqueId" ascending:YES];
	[request setSortDescriptors:[NSArray arrayWithObject:sorter]];

	NSError *__autoreleasing error = nil;
	NSArray * results = [objContext executeFetchRequest:request error:&error];

	return results;
}


/* -----------------------------------------------------------------------------
	Return the entries that have been imported since the last sync.
	An imported entry is asigned a uniqueId greater than kImportBaseId so we can
	query for those.
	After sync, an entry’s uniqueId is updated to match the Newton entry.
	Args:		--
	Return:	array of NCEntry objects
----------------------------------------------------------------------------- */

- (NSArray *) importedEntries
{
	NSManagedObjectContext * objContext = [self managedObjectContext];
	NSFetchRequest * request = [[NSFetchRequest alloc] init];
	[request setEntity:[NSEntityDescription entityForName:@"Entry" inManagedObjectContext:objContext]];
	[request setPredicate:[NSPredicate predicateWithFormat:@"uniqueId > %@ AND soup = %@", [NSNumber numberWithUnsignedInt:kImportIdBase], self]];

	NSError *__autoreleasing error = nil;
	NSArray * results = [objContext executeFetchRequest:request error:&error];

	return results;
}


/* -----------------------------------------------------------------------------
	Return entries in the soup, modified after a given time or with a _uniqueId
	greater than a given id (indicating it was created after that).
	Args:		inTime
				inId
	Return:	NSArray *
----------------------------------------------------------------------------- */

- (NSArray *) entriesLaterThan: (NSDate *) inTime withIdGreaterThan: (NSUInteger) inId
{
	NSManagedObjectContext * objContext = [self managedObjectContext];
	NSFetchRequest * request = [[NSFetchRequest alloc] init];
	[request setEntity:[NSEntityDescription entityForName:@"Entry" inManagedObjectContext:objContext]];
	[request setPredicate:[NSPredicate predicateWithFormat:@"soup = %@ AND (modTime > %@ OR uniqueId > %d)", self, inTime, inId]];

	NSSortDescriptor * sorter = [[NSSortDescriptor alloc] initWithKey:@"uniqueId" ascending:YES];
	[request setSortDescriptors:[NSArray arrayWithObject:sorter]];

	NSError *__autoreleasing error = nil;
	NSArray * results = [objContext executeFetchRequest:request error:&error];

	return results;
}


/* -----------------------------------------------------------------------------
	Delete the soup entry with the given id.
	Args:		inSoup
				inSet
	Return:	--
----------------------------------------------------------------------------- */

- (void) deleteEntryId: (NSUInteger) inId
{
	NSManagedObjectContext * objContext = [self managedObjectContext];
	NSFetchRequest * request = [[NSFetchRequest alloc] init];
	[request setEntity:[NSEntityDescription entityForName:@"Entry" inManagedObjectContext:objContext]];
	[request setPredicate:[NSPredicate predicateWithFormat:@"uniqueId = %d AND soup = %@", inId, self]];

	NSError *__autoreleasing error = nil;
	NSArray * results = [objContext executeFetchRequest:request error:&error];

	if (results.count > 0)
		[objContext deleteObject:[results objectAtIndex:0]];
}


/* -----------------------------------------------------------------------------
	Remove entries from a soup whose _uniqueId does not exist in the given set
	of NSNumbers.
	This enables our NCSoup to be pruned to match a Newton soup.
	Args:		inSoup
				inSet
	Return:	--
----------------------------------------------------------------------------- */

- (void) cropTo: (NSIndexSet *) indexSet
{
	// first create a set of NSNumber*s from the indexSet
	NSMutableSet * theSet = [NSMutableSet setWithCapacity:[indexSet count]];
	NSUInteger uid;
	for (uid = [indexSet firstIndex]; uid != NSNotFound; uid = [indexSet indexGreaterThanIndex:uid])
	{
		[theSet addObject:[NSNumber numberWithUnsignedInteger:uid]];
	}
//NSLog(@" cropping to ids: %@", theSet);

	NSManagedObjectContext * objContext = [self managedObjectContext];
	NSFetchRequest * request = [[NSFetchRequest alloc] init];
	[request setEntity:[NSEntityDescription entityForName:@"Entry" inManagedObjectContext:objContext]];
	[request setPredicate:[NSPredicate predicateWithFormat:@"soup = %@ AND uniqueId < %@ AND NOT uniqueId IN %@", self, [NSNumber numberWithUnsignedInt:kImportIdBase], theSet]];

	NSError *__autoreleasing error = nil;
	NSArray * results = [objContext executeFetchRequest:request error:&error];
//NSLog(@" found entries: %@", results);

	// we now have an array of entries that have an id NOT in the given set; ie thay no longer exist in the set
	if (results.count > 0)
	{
		for (NCEntry * entry in results)
		{
FULL_LOG {
	//NSLog(@" pruning %@", entry);
	REPprintf("\npruning entry id = %u", [entry.uniqueId unsignedIntValue]);
}
			[self removeEntriesObject:entry];
		}
	}
}

@end
