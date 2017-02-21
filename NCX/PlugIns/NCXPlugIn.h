/*
	File:		NCXPlugIn.h

	Contains:	Plugin controller for the NCX app.

	Written by:	Newton Research Group, 2006.
*/

#import <Cocoa/Cocoa.h>
#import "NCXTranslator.h"

@class NCDocument, NCApp;

/*------------------------------------------------------------------------------
	N C X P l u g I n C o n t r o l l e r
------------------------------------------------------------------------------*/

@interface NCXPlugInController : NSObject
{
	NSMutableDictionary * allFileFormats;

	NSMutableDictionary * importFormats;
	NSMutableDictionary * importTranslators;
	NSDictionary * importContext;
	NCXTranslator * importTranslator;

	NSMutableDictionary * exportFormats;
	NSMutableSet * usedTranslators;
	NCApp * theApp;
	NCDocument * theContext;
	NSURL * theURL;
	NSString * theSoupName;
}
@property(nonatomic,retain) NSString * importSoupName;

+ (NCXPlugInController *) sharedController;

// Export methods
- (NSString *) typeForClass: (NSString *) inClass;
- (void) beginExport: (NCApp *) inApp context: (NCDocument *) inDocument destination: (NSURL *) inURL;
- (NSString *) export: (NCEntry *) inEntry;
- (NSString *) endExport;

// Import methods
- (NSArray *) importTypesForApp: (NCApp *) inApp;
- (NSArray *) importFileTypes;
- (NSArray *) applicationNamesForImport: (NSString *) inFileType;
- (void) beginImport: (NCApp *) inApp context: (NCDocument *) inDocument source: (NSURL *) inURL;
- (Ref) import;
- (void) endImport;

// Common
- (NSString *) fileFormat: (NSString *) inFileType;
@end

