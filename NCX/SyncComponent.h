/*
	File:		SyncComponent.h

	Contains:	Declarations for the NCX sync controller.

	Written by:	Newton Research, 2009.
*/

#import <Cocoa/Cocoa.h>
#import <SyncServices/SyncServices.h>
#import "Component.h"
#import "NameTransformer.h"
//#import "DateTransformer.h"

#define kSyncClientId	@"com.newton.connection"

#define kDDoSynchronize	'SYNC'

enum SyncMode
{
	kFastSync,
	kSlowSync,
	kRefreshSync,
	kPullTheTruth
};


@class NCDocument, NCStore;
@interface NCSyncComponent : NCComponent
{
	ISyncClient * syncClient;
	ISyncSession * syncSession;

	BOOL isDesktopInControl;
	BOOL isSyncExtensionInstalled;
	RefStruct syncInfo;
	NewtonErr syncErr;

	NSArray * entityNames;
	NSMutableArray * filteredEntityNames;

	EntityTransformer * txformer;
	NameTransformer * nameTxformer;
//	DateTransformer * dateTxformer;
}

- (BOOL) hasSynced;

- (ISyncClient *) registerSyncClient;

- (void) doBackup: (NCStore *) inStoreObj soup: (NSString *) inSoupName;
- (void) startSyncSession;
- (void) negotiateSession;
- (void) performSync: (ISyncClient *) inSyncClient session: (ISyncSession *) inSyncSession;
- (void) pushData;
- (void) pullData;
- (void) pullChangesFor: (EntityTransformer *) inTransformer entityNames: (NSArray *) inEntityNames;
- (void) cancelSync;

@end
