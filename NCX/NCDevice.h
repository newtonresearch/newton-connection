/*
	File:		DeviceInfo.h

	Contains:	Newton connection device info model.

	Written by:	Newton Research, 2011.
*/

#import <CoreData/CoreData.h>
#import "NCStore.h"
#import "NCUserInfo.h"


@interface NCDevice : NSManagedObject <NCSourceItem>

@property(nonatomic,strong) NSString * name;
@property(nonatomic,strong) NSData * info;
@property(nonatomic,strong) NSString * OSinfo;
@property(nonatomic,strong) NSDate * manufactureDate;
@property(nonatomic,strong) NSString * processor;
@property(nonatomic,strong) NCUserInfo * user;
@property(nonatomic,strong) NSDate * backupDate;		// OSX time
@property(nonatomic,strong) NSNumber * backupTime;		// Newton time (for Newton 1 sync)
@property(nonatomic,strong) NSNumber * backupError;
@property(nonatomic,strong) NSNumber * syncMode;
@property(nonatomic,strong) NSSet<NCStore *> * stores;

// UI representation
@property(nonatomic,readonly)	NSImage *  image;
@property(nonatomic,readonly)	NSString * icon;
@property(nonatomic,readonly)	NSNumber * is1xData;
@property(nonatomic,readonly)	NSString * manufacturer;
@property(nonatomic,readonly)	NSString * machineType;
@property(nonatomic,readonly)	NSString * newtonId;
@property(nonatomic,readonly)	NSString * serialNumber;
@property(nonatomic,readonly)	NSString * ROMversion;
@property(nonatomic,readonly)	NSString * OSversion;
@property(nonatomic,readonly)	NSString * RAMsize;
@property(nonatomic,readonly)	NSString * visibleName;
@property(nonatomic,readonly)	NSString * visibleId;

// device info
@property(nonatomic,readonly)	NSSize screenSize;
@property(nonatomic,strong)	NSMutableArray<NCStore *> * tetheredStores;

- (void) addTetheredStore: (NCStore *) inStore;
@end


@interface NCDevice (CoreDataGeneratedAccessors)

- (void)addStoresObject:(NCStore *)value;
- (void)removeStoresObject:(NCStore *)value;
- (void)addStores:(NSSet<NCStore *> *)value;
- (void)removeStores:(NSSet<NCStore *> *)value;

@end

