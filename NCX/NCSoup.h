/*
	File:		NCSoup.h

	Abstract:	An NCSoup models a Newton soup.
					It contains info about the soup, and its entries.

	Written by:		Newton Research, 2012.
*/

#import <CoreData/CoreData.h>

#import "NCEntry.h"
#import "NCSourceItem.h"

/*
	Newton time is expressed as an integer: the number of minutes since 1 Jan 1904.
*/
typedef uint32_t NewtonTime;

/*
	We need to allocate temporary _uniqueIds for untethered imported soup entries.
	These will be updated when we next sync.
	We start these temporary ids at an implausibly high base:
	the highest valid NewtonScript int is 0x3FFFFFFF = 1073741823
	268435455.75
	so if we round down to 1070000000 that gives 3,741,823 valid ids before the int rolls over.
	We never reset the temporary id allocator; but if the user imported 100 entries into the same soup
	for 100 years, we still wouldn’t run out if ids.
*/
#define kImportIdBase 1070000000


@class NCApp, NCStore;

@interface NCSoup : NSManagedObject <NCSourceItem>
{
//	NCInfoController * viewController;
	NSArray * columnInfo;
}

@property(nonatomic,retain) NSString * name;
@property(nonatomic,retain) NSNumber * signature;
@property(nonatomic,retain) NSData * indexes;
@property(nonatomic,retain) NSData * info;
@property(nonatomic,retain) NSString *	descr;
@property(nonatomic,retain) NSString *	appName;
@property(nonatomic,retain) NSNumber * lastBackupTime;
@property(nonatomic,retain) NSNumber * lastBackupId;
@property(nonatomic,retain) NSNumber * lastImportId;		// for desktop import
@property(nonatomic,retain) NSDate * prevSynchTime;
@property(nonatomic,retain) NSData * prevSynchIdData;
@property(nonatomic,retain) NSMutableSet * entries;
@property(nonatomic,retain) NCApp * app;
@property(nonatomic,retain) NCStore * store;

// last time we were backed up / sync’d
@property(nonatomic,readonly)	NewtonTime lastSyncTime;

// property part of NCSourceItem protocol
@property(readonly)	NSImage * image;
//@property(nonatomic,readonly) NCInfoController * viewController;

// property to support display of entries in NSTableView
@property(retain)	NSArray * columnInfo;

// properties to support SyncServices
@property(nonatomic,retain)	NSIndexSet * prevSynchIds;
@property(nonatomic,readonly)	NSIndexSet * currIds;

// return all entries ordered by uniqueId
- (NSArray *) orderedEntries;
// return all entries with id > kImportIdBase
- (NSArray *) importedEntries;
// return new/modified entries (ordered by uniqueId)
- (NSArray *) entriesLaterThan: (NSDate *) inTime withIdGreaterThan: (NSUInteger) inId;
// delete all entries not in indexSet
- (void) cropTo: (NSIndexSet *) indexSet;

- (void) deleteEntryId: (NSUInteger) inId;
@end


@interface NCSoup (CoreDataGeneratedAccessors)
- (void)addEntriesObject:(NCEntry *)value;
- (void)removeEntriesObject:(NCEntry *)value;
- (void)addEntries:(NSSet *)value;
- (void)removeEntries:(NSSet *)value;
@end

