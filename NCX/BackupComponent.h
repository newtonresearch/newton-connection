/*
	File:		BackupComponent.h

	Contains:	Declarations for the NCX backup and restore controllers.

	Written by:	Newton Research, 2009.
*/

#import "NCDockProtocolController.h"

#define kDDoRequestToSync			'SSYN'
#define kDDoRestore					'RSTR'


@interface NCBackupComponent : NCComponent
{
}
- (void) doBackup;
@end


@interface NCRestoreComponent : NCComponent
{
	RefStruct syncInfo;
	char * restorePath;
}
- (void) setRestorePath: (NSString *) inPath;
- (void) doRestore;
@end
