/*
	PkgPart.mm
	Newton package inspector.

	Written by Newton Research, 2008.
*/

#if __LITTLE_ENDIAN__
#define hasByteSwapping 1
#endif
#define __MACMEMORY__

#import "PkgPart.h"
#import "NewtonKit.h"
#import "Utilities.h"

extern bool		FromObject(RefArg inObj, Rect * outBounds);
extern void		InitDrawing(CGContextRef inContext, int inScreenHeight);
extern void		DrawBitmap(RefArg inBitmap, const Rect * inRect, int inTransferMode);


/*------------------------------------------------------------------------------
	D a t a
------------------------------------------------------------------------------*/

extern NSNumberFormatter * gNumberFormatter;
extern NSDateFormatter * gDateFormatter;

#define kMinutesSince1904   34714080
#define kSecondsSince1904 2082844800



/* -----------------------------------------------------------------------------
	P k g I n f o
	Top-level package info object - contains info on the parts too.
----------------------------------------------------------------------------- */

@implementation PkgInfo

- (id)initWithDirectory:(const PackageDirectory	*)inPkgDirectory {
	if (self = [super init]) {
		// read relevant info out of directory into model instance variables
		int dataOffset = sizeof(PackageDirectory) + inPkgDirectory->numParts * sizeof(PartEntry);
		_ident = [NSString stringWithFormat: @"%u", inPkgDirectory->id];
		_isCopyProtected = (inPkgDirectory->flags & kCopyProtectFlag) != 0;
		_version = [NSString stringWithFormat: @"%u", inPkgDirectory->version];
		_copyright = [NSString stringWithCharacters: (UniChar *)((char *)inPkgDirectory + dataOffset + inPkgDirectory->copyright.offset) length: inPkgDirectory->copyright.length/sizeof(UniChar)];
		_name =  [NSString stringWithCharacters: (UniChar *)((char *)inPkgDirectory + dataOffset + inPkgDirectory->name.offset) length: inPkgDirectory->name.length/sizeof(UniChar)];
		_size = [NSString stringWithFormat: @"%@ bytes", [gNumberFormatter stringFromNumber: [NSNumber numberWithInt: inPkgDirectory->size]]];
		_creationDate = [gDateFormatter stringFromDate: [NSDate dateWithTimeIntervalSince1970: ((NSTimeInterval)inPkgDirectory->creationDate - kSecondsSince1904)]];

		ArrayIndex partCount = inPkgDirectory->numParts;
		if (partCount == 0) {
			partCount = 1;
		}
		_parts = [[NSMutableArray alloc] initWithCapacity:partCount];
	}
	return self;
}


- (void)addPart:(PkgPart *)inPart {
	[_parts addObject:inPart];
}

@end


/* -----------------------------------------------------------------------------
	P k g P a r t
----------------------------------------------------------------------------- */

@implementation PkgPart

- (id)init:(const PartEntry *)inPart ref:(Ref)inRef data:(char *)inData sequence:(unsigned int)inSeq {
	if (self = [super init]) {
		ULong partFlagged = inPart->flags & 0x07;
		_partType = inPart->type;
		NSString * typeStr = _partType ? [NSString stringWithFormat: @"%c%c%c%c", (_partType >> 24) & 0xFF, (_partType >> 16) & 0xFF, (_partType >> 8) & 0xFF, _partType & 0xFF] : nil;
		if (partFlagged == kProtocolPart)
			typeStr = typeStr ? [NSString stringWithFormat: @"%@ protocol", typeStr] : @"protocol";
		else if (partFlagged == kRawPart)
			typeStr = typeStr ? [NSString stringWithFormat: @"raw %@", typeStr] : @"raw";
		if (typeStr == nil)
			typeStr = @"unspecified type";
		_partTitle = [[NSString alloc] initWithFormat: @"Part %d  (%@)", inSeq, typeStr];
		_size = [[NSString alloc] initWithFormat: @"%@ bytes", [gNumberFormatter stringFromNumber: [NSNumber numberWithInt:inPart->size]]];
		_iconImage = nil;

		if ((inPart->flags & kNOSPart) != 0)
			_rootRef = inRef;
		else
			_rootRef = NILREF;

		_data = inData;
	}
	return self;
}


- (void) dealloc {
	if (_data)
		free(_data);
}


- (NSImage *) iconImage {	// should be in PkgFormPart?
	if (_iconImage == nil && NOTNIL(self.rootRef)) {
		RefVar icon(GetFrameSlot(self.rootRef, MakeSymbol("iconPro")));
		if (NOTNIL(icon))
			icon = GetFrameSlot(icon, MakeSymbol("unhilited"));
		if (ISNIL(icon))
			icon = GetFrameSlot(self.rootRef, MakeSymbol("icon"));
		if (NOTNIL(icon)) {
			Rect boundsRect;
			FromObject(GetFrameSlot(icon, MakeSymbol("bounds")), &boundsRect);

			_iconImage = [[NSImage alloc] initWithSize: NSMakeSize(boundsRect.right,  boundsRect.bottom)];
			[_iconImage lockFocus];
	 
			InitDrawing((CGContextRef)NSGraphicsContext.currentContext.graphicsPort, boundsRect.bottom);
			DrawBitmap(icon, &boundsRect, 0/*modeCopy*/);
	 
			[_iconImage unlockFocus];
		}
	}
	return _iconImage;
}

@end


/*------------------------------------------------------------------------------
	P k g F o r m P a r t
------------------------------------------------------------------------------*/

@implementation PkgFormPart

- (id)init:(const PartEntry *)inPart ref:(Ref)inRef data:(char *)inData sequence:(unsigned int)inSeq {
	if (self = [super init:inPart ref:inRef data:inData sequence:inSeq]) {
		_text = MakeNSString(GetFrameSlot(self.rootRef, MakeSymbol("text")));
		// preflight the icon
		[self iconImage];
	}
	return self;
}

@end


/*------------------------------------------------------------------------------
	P k g B o o k P a r t
------------------------------------------------------------------------------*/

@implementation PkgBookPart

- (id)init:(const PartEntry *)inPart ref:(Ref)inRef data:(char *)inData sequence:(unsigned int)inSeq {
	if (self = [super init:inPart ref:inRef data:inData sequence:inSeq]) {
		RefVar book(GetFrameSlot(self.rootRef, MakeSymbol("book")));
		Ref dateRef = GetFrameSlot(book, MakeSymbol("publicationDate"));
		if (ISINT(dateRef)) {
			NSTimeInterval interval = RVALUE(dateRef);
			_date = [gDateFormatter stringFromDate: [NSDate dateWithTimeIntervalSince1970: (interval - kMinutesSince1904)*60]];
		} else {
			_date = nil;
		}

		_title = MakeNSString(GetFrameSlot(book, MakeSymbol("title")));
		_isbn = MakeNSString(GetFrameSlot(book, MakeSymbol("ISBN")));
		_author = MakeNSString(GetFrameSlot(book, MakeSymbol("author")));
		_copyright = MakeNSString(GetFrameSlot(book, MakeSymbol("copyright")));
	}
	return self;
}

@end


/*------------------------------------------------------------------------------
	P k g S o u p P a r t
------------------------------------------------------------------------------*/

@implementation PkgSoupPart
@end

