/*
	File:		NCRestoreInfo.h

	Abstract:	Interface for NCRestoreInfo class.
					IB bindings:
						device store array controller -> restoreInfo.allStores
							device store popup content -> dsac.arrangedObjects
							device store popup content values -> dsac.arrangedObjects .store.name
							device store popup selected object -> restoreInfo.store
						backup store array controller -> restoreInfo.allRestores
							backup store popup content -> bsac.arrangedObjects
							backup store popup content values -> bsac.arrangedObjects .name
							backup store popup selected object -> restoreInfo.restore

	Written by:		Newton Research, 2012.
*/

#import "NCStore.h"


@interface NCRestoreInfo : NSObject
@property(nonatomic,assign) NSDictionary * store;		// selected dev store
@property(nonatomic,assign) NSDictionary * restore;	// selected backup/restore info

@property(readonly) NSArray * allStores;
@property(readonly) NSArray * allRestores;	// .backups of current store
@property(readonly) NCStore * sourceStore;
@property(readonly) NSArray * apps;
@property(readonly) NSArray * pkgs;

- (id)initStoreInfo:(NSArray *)info;
@end
