/*
	File:		NCStore.h

	Abstract:	An NCStore models a Newton store.
					It contains info about the store, and lists of the soups on that store.

	Written by:		Newton Research, 2012.
*/

#import <CoreData/CoreData.h>
#import "NCApp.h"

@interface NCStore : NSManagedObject <NCSourceItem>
{
//	NCInfoController * viewController;
}

@property(nonatomic,retain) NSString * name;
@property(nonatomic,retain) NSString * kind;
@property(nonatomic,retain) NSNumber * readOnly;
@property(nonatomic,retain) NSNumber * defaultStore;
@property(nonatomic,retain) NSNumber * signature;
@property(nonatomic,retain) NSString * storePassword;
@property(nonatomic,retain) NSNumber * storeVersion;
@property(nonatomic,retain) NSNumber * usedSize;
@property(nonatomic,retain) NSNumber * totalSize;
@property(nonatomic,retain) NSMutableSet * soups;

// UI representation
@property(nonatomic,readonly)	NSImage *  image;
@property(nonatomic,readonly)	NSString * icon;
//@property(nonatomic,readonly) NCInfoController * viewController;
@property(nonatomic,readonly)	NSString * status;
@property(nonatomic,readonly)	NSString * capacity;
@property(nonatomic,readonly)	NSString * used;
@property(nonatomic,readonly)	NSString * free;

@property(nonatomic,readonly)	BOOL isReadOnly;
@property(nonatomic,readonly)	BOOL isInternal;
@property(nonatomic,readonly)	BOOL isDefault;

// comparison
- (BOOL)isEqualTo:(NCStore *)inStore;

- (NCSoup *)findSoup:(NSString *)inName;
- (NSArray *)soupsInApp:(NCApp *)inApp;

// app fetching (for restore dialog)
- (NSArray *)pkgs;
- (NSArray *)apps:(BOOL)includePkgs;

@end


@interface NCStore (CoreDataGeneratedAccessors)

- (void)addSoupsObject:(NCSoup *)value;
- (void)removeSoupsObject:(NCSoup *)value;
- (void)addSoups:(NSSet *)value;
- (void)removeSoups:(NSSet *)value;

@end

