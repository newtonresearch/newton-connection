/*
	File:		NCXTranslator.mm

	Contains:   Import/Export translator plugin interface.

	Written by: Newton Research Group, 2006.
*/

#import "NCXTranslator.h"
#import "NCDocument.h"
#import "PlugInUtilities.h"


/* -----------------------------------------------------------------------------
	N C X T r a n s l a t o r
----------------------------------------------------------------------------- */

NSDateFormatter * gTxDateFormatter;

@implementation NCXTranslator

+ (BOOL)aggregatesEntries {
	return NO;
}


+ (void) initialize {
	// initialize the date formatter used throughout the UI
	gTxDateFormatter = [[NSDateFormatter alloc] init];
	[gTxDateFormatter setDateStyle:NSDateFormatterMediumStyle];
}


- (void)beginImport:(NSURL *)inURL context:(NCDocument *)inDocument {
	// retained by the plugin controller
	theURL = inURL;
	theContext = inDocument;
}

- (Ref)import {
	return NILREF;
}

- (void)importDone {
}


- (void)beginExport:(NSString *)inAppName context:(NCDocument *)inDocument destination:(NSURL *)inURL {
	// retained by the plugin controller
	theURL = inURL;
	theContext = inDocument;
}

- (NSString *)export:(RefArg)inEntry {
	return nil;
}

- (NSString *)exportDone:(NSString *)inAppName {
	return nil;
}


/* -----------------------------------------------------------------------------
	Create a name for the desktop file from the Notes entry title.
	Args:		inEntry			Notes soup entry
	Return:	--					filename string
----------------------------------------------------------------------------- */

- (NSString *)makeFilename:(RefArg)inEntry {
	NSString * titleStr = MakeNSString(GetFrameSlot(inEntry, SYMA(title)));
	if (titleStr == nil) {
		RefVar timestamp(GetFrameSlot(inEntry, SYMA(timestamp)));
		if (NOTNIL(timestamp))
			titleStr = [gTxDateFormatter stringFromDate:MakeNSDate(timestamp)];
		else
			titleStr = NSLocalizedString(@"untitled", nil);
	}
	return titleStr;
}



- (NSString *)write:(NSData *)inData toFile:(NSString *)inName extension:(NSString *)inExtension {

	NSMutableString * filename = [[NSMutableString alloc] init];
	filename.string = [inName stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceCharacterSet]];
	if (filename.length > 0) {
		// ensure path does not contain path separator : or /
		NSCharacterSet * set = [NSCharacterSet characterSetWithCharactersInString:@":/"];
		NSRange searchRange = NSMakeRange(0, filename.length);
		NSRange characterRange;
		while (characterRange = [filename rangeOfCharacterFromSet:set options:NSLiteralSearch range:searchRange], characterRange.length > 0)
		{
			[filename replaceCharactersInRange:characterRange withString:@"-"];
			searchRange.location = characterRange.location + 1;
			searchRange.length = filename.length - searchRange.location;
			if (searchRange.length == 0)
				break; // Might as well save that extra method call.
		}
	}
	else
		filename.string = NSLocalizedString(@"untitled", nil);

	NSString * hashedName = [NSString stringWithFormat:@"%@######.%@", filename,inExtension];
	NSRange theRange = [hashedName rangeOfString:@"######"];
	[filename appendFormat:@".%@", inExtension];

	for (int sequence = 1; sequence < 1000; ++sequence) {	// provide emergancy stop at 1000 iterations
		NSError * __autoreleasing err = nil;
		NSURL * docURL = [theURL URLByAppendingPathComponent:filename];
		if ([inData writeToURL:docURL options:NSDataWritingWithoutOverwriting error:&err]) {
			NSDictionary * fileAttrs = @{ NSFileExtensionHidden:[NSNumber numberWithBool:YES] };
			[NSFileManager.defaultManager setAttributes:fileAttrs ofItemAtPath:docURL.path error:&err];
			break;
		}
		// try next filename
		filename = [hashedName mutableCopy];
		[filename replaceCharactersInRange:theRange withString:[NSString stringWithFormat:@"-%d", sequence]];
	}
	return [NSString stringWithString:filename];
}

@end


/* -----------------------------------------------------------------------------
	N C D e f a u l t T r a n s l a t o r
----------------------------------------------------------------------------- */
extern void		RedirectStdioOutTranslator(FILE * inFRef);
extern Ref		gVarFrame;
extern Ref *	RSgVarFrame;

@interface NCDefaultTranslator ()
{
	NSString * filename;
	FILE * newtout;
	Ref savedPrintDepth;
	Ref savedPrintLength;
}
@end

@implementation NCDefaultTranslator

- (void)beginExport:(NSString *)inAppName context:(NCDocument *)inDocument destination:(NSURL *)inURL {
	[super beginExport:inAppName context:inDocument destination:inURL];

//	NSDictionary * fileAttrs = @{ NSFileExtensionHidden:[NSNumber numberWithBool:YES] };
	filename = [NSString stringWithFormat:@"Newton %@", inAppName];
	if ((filename = [self write:[NSData data] toFile:filename extension:@"text"]) != nil) {
		NSURL * path = [theURL URLByAppendingPathComponent:filename];
		newtout = fopen(path.fileSystemRepresentation, "w");
		if (newtout)
			RedirectStdioOutTranslator(newtout);
		savedPrintDepth = GetFrameSlot(RA(gVarFrame), SYMA(printDepth));
		savedPrintLength = GetFrameSlot(RA(gVarFrame), SYMA(printLength));
		SetFrameSlot(RA(gVarFrame), SYMA(printDepth), MAKEINT(16));
		SetFrameSlot(RA(gVarFrame), SYMA(printLength), RA(NILREF));
	} else {
		newtout = NULL;
	}
}


- (NSString *)export:(RefArg)inEntry {
	if (newtout) {
		newton_try
		{
			PrintObject(inEntry, 0);
		}
		newton_catch_all
		{
			REPprintf("\n*** Error printing object (%d). ***\n", CurrentException()->data);
		}
		end_try;
		REPprintf("\n\n");
	}
	return nil;
}


- (NSString *)exportDone:(NSString *)inAppName {
	SetFrameSlot(RA(gVarFrame), SYMA(printDepth), savedPrintDepth);
	SetFrameSlot(RA(gVarFrame), SYMA(printLength), savedPrintLength);
	if (newtout) {
		RedirectStdioOutTranslator(NULL);
		fclose(newtout);
	}
	return filename;
}

@end


/* -----------------------------------------------------------------------------
	N C T e x t T r a n s l a t o r
----------------------------------------------------------------------------- */

@implementation NCTextTranslator

- (NSString *)export:(RefArg)inEntry {
	NSString * filename = nil;
	// create package file -- inEntry.pkgRef -> NSData
	NSString * text = MakeNSString(GetFrameSlot(inEntry, SYMA(text)));
	if (text) {
		filename = MakeNSString(GetFrameSlot(inEntry, SYMA(title)));
		if (filename == nil)
			filename = @"Text";
		filename = [filename stringByAppendingPathExtension:@"text"];
		NSURL * path = [theURL URLByAppendingPathComponent:filename];
		NSError *__autoreleasing error = nil;
		if (![text writeToURL:path atomically:YES encoding:NSUTF8StringEncoding error:&error]) {
			filename = nil;
		}
	}
	return filename;
}

@end


/* -----------------------------------------------------------------------------
	N C P a c k a g e T r a n s l a t o r
----------------------------------------------------------------------------- */

@implementation NCPackageTranslator

- (NSString *)export:(RefArg)inEntry {
	NSString * filename = nil;
	// create package file -- inEntry.pkgRef -> NSData
	RefVar pkgRef(GetFrameSlot(inEntry, MakeSymbol("pkgRef")));
	if (IsBinary(pkgRef)) {
		NSData * pkgData;
		WITH_LOCKED_BINARY(pkgRef, pkgPtr)
		pkgData = [NSData dataWithBytesNoCopy:pkgPtr length:Length(pkgRef) freeWhenDone:NO];
		END_WITH_LOCKED_BINARY(pkgRef)

		NSString * filename = MakeNSString(GetFrameSlot(inEntry, MakeSymbol("packageName")));
		if (filename == nil)
			filename = @"Newton Package";
		if (![self write:pkgData toFile:filename extension:@"newtonpkg"]) {
			filename = nil;
		}
	}
	return filename;
}

@end


/* -----------------------------------------------------------------------------
	N C I m a g e T r a n s l a t o r
----------------------------------------------------------------------------- */
extern bool		FromObject(RefArg inObj, Rect * outBounds);
extern void		InitDrawing(CGContextRef inContext, int inScreenHeight);
extern void		DrawBitmap(RefArg inBitmap, const Rect * inRect, int inTransferMode);

@implementation NCImageTranslator

- (NSString *)export:(RefArg)inEntry {
	NSString * filename = nil;
	newton_try
	{
		// create image file
		RefVar image(GetFrameSlot(inEntry, MakeSymbol("image")));
		CDataPtr boundsObj(GetFrameSlot(image, MakeSymbol("bounds")));
		Rect		boundsRect = *(Rect *)(Ptr)boundsObj;
#if defined(hasByteSwapping)
		boundsRect.top = BYTE_SWAP_SHORT(boundsRect.top);
		boundsRect.left = BYTE_SWAP_SHORT(boundsRect.left);
		boundsRect.bottom = BYTE_SWAP_SHORT(boundsRect.bottom);
		boundsRect.right = BYTE_SWAP_SHORT(boundsRect.right);
#endif
		NSImage * img = [[NSImage alloc] initWithSize:NSMakeSize(boundsRect.right,boundsRect.bottom)];
		[img lockFocus];
		InitDrawing((CGContextRef) [[NSGraphicsContext currentContext] graphicsPort], boundsRect.bottom);
		DrawBitmap(image, &boundsRect, 0);
		[img unlockFocus];

		NSData * tiffData = [img TIFFRepresentationUsingCompression: NSTIFFCompressionLZW factor: 1.0];
		if (tiffData) {
			filename = MakeNSString(GetFrameSlot(inEntry, MakeSymbol("title")));
			if (filename == nil)
				filename = @"Newton Image";
			if (![self write:tiffData toFile:filename extension:@"tiff"]) {
				filename = nil;
			}
		}
	}
	newton_catch_all
	{
		filename = nil;
	}
	end_try;
	return filename;
}

@end

