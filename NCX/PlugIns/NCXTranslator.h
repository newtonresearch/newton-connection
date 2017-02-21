/*
	File:		NCXTranslator.h

	Contains:   Import/Export translator plugin interface.

	Written by: Newton Research Group, 2006.
*/

#import "NCDocument.h"

extern NSDateFormatter * gTxDateFormatter;


@interface NCXTranslator : NSObject
{
	NSURL * theURL;
	NCDocument * theContext;
}
+ (BOOL)aggregatesEntries;
- (NSString *)makeFilename:(RefArg)inEntry;
- (NSString *)write:(NSData *)inData toFile:(NSString *)inName extension:(NSString *)inExtension;

- (void)beginImport:(NSURL *)inURL context:(NCDocument *)inDocument;
- (void)beginExport:(NSString *)inAppName context:(NCDocument *)inDocument destination:(NSURL *)inURL;

// subclasses must implement either:
- (Ref)import;
- (void)importDone;
// or:
//+ (BOOL)aggregatesEntries; optional
- (NSString *)export:(RefArg)inEntry;
- (NSString *)exportDone:(NSString *)inAppName;
@end


@interface NCDefaultTranslator : NCXTranslator
@end


@interface NCTextTranslator : NCXTranslator
@end


@interface NCPackageTranslator : NCXTranslator
@end


@interface NCImageTranslator : NCXTranslator
@end
