/*
	File:		NCRestoreInfo.m

	Abstract:	Implementation of NCRestoreInfo class.

	Written by:		Newton Research, 2012.
*/

#import "NCRestoreInfo.h"


@implementation NCRestoreInfo

+ (NSSet<NSString *> *)keyPathsForValuesAffectingAllRestores {
	return [NSSet setWithObject:@"store"];
}

+ (NSSet<NSString *> *)keyPathsForValuesAffectingApps {
	return [NSSet setWithObject:@"restore"];
}

+ (NSSet<NSString *> *)keyPathsForValuesAffectingPkgs {
	return [NSSet setWithObject:@"restore"];
}


/* -----------------------------------------------------------------------------
	Initialize with information about tethered stores.
	Args:		info		array of tethered stores, Internal first
	Return:	self
----------------------------------------------------------------------------- */

- (id)initStoreInfo:(NSArray *)info {
	if (self = [super init]) {
		_allStores = info;
		if (self.allStores.count > 0) {
			_store = self.allStores[0];
			_restore = self.allRestores[0];
		} else {
			_store = nil;
			_restore = nil;
		}
	}
	return self;
}


/* -----------------------------------------------------------------------------
	Return all backups we can restore to the currently selected store.
	Args:		--
	Return:	array of store dictionaries
----------------------------------------------------------------------------- */

- (NSArray *)allRestores {
	return self.store[@"backups"];
}


- (void)setStore:(NSDictionary *)inStoreDict {
	_store = inStoreDict;	// avoid infinite loop, don’t use property accessor
	self.restore = self.allRestores[0];
}


- (NCStore *)sourceStore {
	if (self.restore) {
		return self.restore[@"store"];
	}
	return nil;
}


- (NSArray *)apps {
	if (self.restore) {
		return self.restore[@"apps"];
	}
	return nil;
}


- (NSArray *)pkgs {
	if (self.restore) {
		return self.restore[@"pkgs"];
	}
	return nil;
}

@end
