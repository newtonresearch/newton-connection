/*
	File:		Works.mm

	Contains:   NewtonWorks import/export functions.

	Written by: Newton Research Group, 2006.
*/

#import "NCXTranslator.h"
#import "PlugInUtilities.h"
#import "NSString-Extensions.h"

extern Ref	SPrintObject(RefArg inObj);

#define hasVBOs 1

extern Ref			GetStores(void);
extern "C" Ref		FLBAllocCompressed(RefArg inStoreWrapper, RefArg inClass, RefArg inLength, RefArg inCompanderName, RefArg inCompanderParms);

extern void		InitDrawing(CGContextRef inContext, int inScreenHeight);
extern void		DrawBitmap(RefArg inBitmap, const Rect * inRect, int inTransferMode);


/*------------------------------------------------------------------------------
	T X D a t a
	For reading text out of works data.
	The format of txData is:
	UInt8		txData type
	UInt16	number of 1K chunks in txText
	struct {
		UInt16	number of UniChars in chunk
		UInt16	chunk index
	} []
	terminated by a 0:0 chunk specifier

Actual data, for example:
30 / 1K     001E
02 | 0001 | 001E 0000 | 0000

360 / 1K    0168
02 | 0001 | 0168 0000 | 0000

584 / 2K    0252
02 | 0002 | 01DF 0000 | 0069 0001 | 0000

02 | 0002 | 00BB 0000 | 0000	!!

866 / 2K    0362
02 | 0002 | 01F3 0000 | 016F 0001 | 0000
^
txData type
     ^
	  number of 1K chunks in txText
	          ^
				 number of UniChars in first chunk
								^
								number of UniChars in next chunk
actual end of text 06E0

2144 / 5K
03 | 0005 | 01A9 0000 | 0173 0001 | 01FF 0003 | 0145 0004 | 0000
01 28 00 00 00 81
01 0D 15 16
01 18 24 1B
01 12 1D
01 17 1D 15
01 0F 0E 18
01 0D 0E 18
01 0D 0E 17
01 0D 0E 08 17 … [158]

------------------------------------------------------------------------------*/

#if 0
// here’s why we should not create txText until we can create VBOs
TXChars *
TXView::InternalizeChars(RefArg inData)
{
	TXChars * txChars = f30->f08;	// Textension * f30;
	TXChars * originalTxChars = txChars;

	RefVar theText;
	if (NOTNIL(theText = GetFrameSlot(inData, SYMtxText)))
	{
		RefVar r7(f58);
		f58 = FGetBinaryStore(RA(NILREF), txText);
		if (NOTNIL(r7))
			txChars = new TXVBOChars(f58);
		if (txChars != nil)
			txChars->setChars(txText);
	}
	else if (NOTNIL(theText = GetFrameSlot(inData, SYMtext)))
	{
		if (NOTNIL(r6))
		{
			r6 = NILREF;
			txChars = new TXBinaryChars(theText);
		}
		else
			txChars->f04 = theText;
	}
	else
		throw2(exRoot, -8701);

	if (txChars == nil)
		OutOfMemory();

	if (txChars != originalTxChars)
		f30->setCharsHandler(txChars);

	return txChars;
}
#endif

@interface TXData : NSObject
{
	unsigned char * p;
	unsigned char dataType;
	unsigned int numOfBlocks;
	unsigned int sizeOfData;
	NSRange currentBlock;
}
+ (Ref) txData: (unsigned int) inSize;
- (id) init: (unsigned char *) inData;
- (unsigned int) readWord;
- (NSRange) next;
@end


@implementation TXData
+ (Ref) txData: (unsigned int) inSize
{
	RefVar txData(AllocateBinary(SYMA(binary), 9));
	WITH_LOCKED_BINARY(txData, p)
	unsigned char * data = (unsigned char *) p;
	unsigned int numOfKBytes = inSize / KByte + 1;
	unsigned int word;
	*data++ = 0x02;											// type of txData
	word = numOfKBytes;
	*data++ = word >> 8;  *data++ = word & 0xFF;		// number of 1K chunks in txText
	word = (inSize & (KByte-1)) / sizeof(UniChar);
	*data++ = word >> 8;  *data++ = word & 0xFF;		// number of unichars in last chunk
	word = numOfKBytes - 1;
	*data++ = word >> 8;  *data++ = word & 0xFF;		// index of last chunk
	END_WITH_LOCKED_BINARY(txData)
	return txData;
}

- (id) init: (unsigned char *) inData
{
	if (self = [super init])
	{
		p = inData;
		dataType = *p++;
		numOfBlocks = [self readWord];
		sizeOfData = numOfBlocks * 512;
		currentBlock = NSMakeRange(0,0);
	}
	return self;
}

- (unsigned int) readWord
{
	unsigned int hi = *p++;
	unsigned int lo = *p++;
	return (hi << 8) + lo;
}

- (NSRange) next
{
	if (currentBlock.length != 0)
	{
		currentBlock.location = ALIGN(NSMaxRange(currentBlock), 512);
		if (currentBlock.location == sizeOfData)
 		{
			currentBlock.length = 0;
			return currentBlock;
		}
	}
	unsigned int end = [self readWord];
	if (end == 0)
	{
		if (currentBlock.location == sizeOfData)
 			currentBlock.length = 0;
		else
			currentBlock.length = sizeOfData - currentBlock.location;
	}
	else
		currentBlock.length = ([self readWord] * 512 - currentBlock.location) + end;
	return currentBlock;
}
@end



/*------------------------------------------------------------------------------
	W o r k s T o R T F
------------------------------------------------------------------------------*/

@interface WorksToRTF : NCXTranslator
{
	NSDictionary * defaultStyle;
	NSMutableDictionary * docAttributes;
	NSMutableAttributedString * worksText;
	BOOL hasGraphics;
	NSImage * worksImage;
	NSMutableString * worksCSV;
}
- (void) translatePaper: (RefArg) inEntry;
- (void) translateDrawPaper: (RefArg) inEntry;
- (void) translateSpreadsheet: (RefArg) inEntry;
- (void) addMargin: (NSString *) inKey from: (RefArg) inFrame slot: (const char *) inTag;
@end


@implementation WorksToRTF

- (id)init
{
	if (self = [super init]) {
		defaultStyle = @{ NSFontAttributeName:[NSFont fontWithName:@"HelveticaNeue" size:12.0] };
	}
	return self;
}


- (NSString *)writeFileWrapper:(NSFileWrapper *)inData toFile:(NSString *)inName extension:(NSString *)inExtension {

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
		NSError __autoreleasing * err = nil;
		NSURL * docURL = [theURL URLByAppendingPathComponent:filename];
		if ([inData writeToURL:docURL options:0 originalContentsURL:nil error:&err]) {
			NSDictionary * fileAttrs = @{ NSFileExtensionHidden:[NSNumber numberWithBool:YES] };
			[[NSFileManager defaultManager] setAttributes:fileAttrs ofItemAtPath:docURL.path error:&err];
			break;
		}
		// try next filename
		filename = [hashedName mutableCopy];
		[filename replaceCharactersInRange:theRange withString:[NSString stringWithFormat:@"-%d", sequence]];
	}
	return [NSString stringWithString:filename];
}


/*------------------------------------------------------------------------------
	Convert NewtonWorks soup entry to RTF.
	Works entries are saved as individual files.
	Args:		inEntry			Works soup entry
	Return:	--					attributed text is saved under the entry’s title
------------------------------------------------------------------------------*/

- (NSString *) export: (RefArg) inEntry
{
	NSString * filewritten = nil;
	newton_try
	{
		RefVar entryClass(ClassOf(inEntry));
		if (EQRef(entryClass, MakeSymbol("paper")))
		{
			docAttributes = nil;
 			worksText = nil;
			hasGraphics = NO;

			[self translatePaper: inEntry];

 			if (worksText != nil && hasGraphics)
			{
				NSError __autoreleasing * err = nil;
				NSFileWrapper * fileWrapper = [worksText fileWrapperFromRange:NSMakeRange(0, worksText.length) documentAttributes:@{NSDocumentTypeDocumentAttribute: NSRTFDTextDocumentType} error:&err];
				filewritten = [self writeFileWrapper:fileWrapper toFile:[self makeFilename:inEntry] extension:@"rtfd"];
				worksText = nil;
			}
 			if (worksText != nil)
			{
				NSData * rtfData = [worksText RTFFromRange: NSMakeRange(0, worksText.length) documentAttributes: docAttributes];
				filewritten = [self write:rtfData toFile:[self makeFilename:inEntry] extension:@"rtf"];
				worksText = nil;
			}
		}
		else if (EQRef(entryClass, MakeSymbol("drawPaper")))
		{
 			worksImage = nil;

			[self translateDrawPaper: inEntry];

			if (worksImage != nil)
			{
				NSData * tiffData = [worksImage TIFFRepresentationUsingCompression: NSTIFFCompressionLZW factor: 1.0];
				filewritten = [self write:tiffData toFile:[self makeFilename:inEntry] extension:@"tiff"];
				worksImage = nil;
			}
		}
		else if (EQRef(entryClass, MakeSymbol("QFNewt:donv")))
		{
 			worksCSV = nil;

			[self translateSpreadsheet: inEntry];

 			if (worksCSV != nil)
			{
				NSData * csvData = [worksCSV dataUsingEncoding:NSUTF8StringEncoding];
				filewritten = [self write:csvData toFile:[self makeFilename:inEntry] extension:@"csv"];
				worksCSV = nil;
			}
		}
	}
	newton_catch_all
	{}
	end_try;
	return filewritten;
}


/*------------------------------------------------------------------------------
	Convert NewtonWorks soup entry of class 'paper to RTF.
	Args:		inEntry			Works soup entry
	Return:	--
				worksText and docAtributes are set up
------------------------------------------------------------------------------*/

- (void) translatePaper: (RefArg) inEntry
{
	NSString * str;
	RefVar data(GetFrameSlot(inEntry, MakeSymbol("saveData")));
	RefVar text(GetFrameSlot(data, MakeSymbol("txText")));
	if (NOTNIL(text))
	{
		// text is in a large binary object, class 'text, grown in 1K chunks, actual size encoded in txData
//		TXData * textInfo = [[TXData alloc] init: (unsigned char *) BinaryData(GetFrameSlot(data, MakeSymbol("txData")))];
		TXData * textInfo = [[TXData alloc] init];
		textInfo = [textInfo init: (unsigned char *) BinaryData(GetFrameSlot(data, MakeSymbol("txData")))];
		UniChar * tx = (UniChar *) BinaryData(text);
#if defined(hasByteSwapping)
		unsigned int numOfChars = Length(text)/sizeof(UniChar);
		for (UniChar * p = tx; p < tx + numOfChars; p++)
			*p = BYTE_SWAP_SHORT(*p);
#endif
		str = [NSString string];
		NSRange txRange;
		// iterate over textInfo gathering text blocks into string
		while (txRange = [textInfo next], txRange.length != 0)
			str = [str stringByAppendingString: [NSString stringWithCharacters: tx + txRange.location length: txRange.length]];
	}
	else if (NOTNIL(text = GetFrameSlot(data, MakeSymbol("text"))))
	{
		// text is a string
		// will already have been byte-swapped by endpoint
#if 0 // defined(hasByteSwapping)
		UniChar * tx = (UniChar *) BinaryData(text);
		unsigned int numOfChars = Length(text)/sizeof(UniChar);
		for (UniChar * p = tx; p < tx + numOfChars; p++)
			*p = BYTE_SWAP_SHORT(*p);
#endif
		str = MakeNSString(text);
	}
	else
	{
		// WTF?
		return;
	}
	worksText = [[NSMutableAttributedString alloc] initWithString: str attributes: nil];

	RefVar styles(GetFrameSlot(data, SYMA(styles)));
	if (IsArray(styles))
	{
		NSRange styleRange = { 0, 0 };
		for (int i = 0, count = Length(styles); i < count; i += 2)
		{
			styleRange.length = RINT(GetArraySlot(styles, i));
			RefVar aStyle(GetArraySlot(styles, i+1));
			if (EQRef(ClassOf(aStyle), MakeSymbol("graphics")))
			{
				RefVar shape(GetFrameSlot(aStyle, MakeSymbol("shape")));
				// render aStyle frame into image
				CDataPtr boundsObj(GetFrameSlot(shape, MakeSymbol("bounds")));
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
				DrawBitmap(shape, &boundsRect, 0);
				[img unlockFocus];

				NSData * tiffData = [img TIFFRepresentationUsingCompression: NSTIFFCompressionLZW factor: 1.0];
				if (tiffData) {
					NSError __autoreleasing * err = nil;
					NSURL * imgURL = ApplicationSupportFile([NSString stringWithFormat:@"graphic-%d.tiff", i/2+1]);
					if ([tiffData writeToURL:imgURL options:0 error:&err]) {
						// create NSFileWrapper for that
						NSFileWrapper * wrapper = [[NSFileWrapper alloc] initWithURL:imgURL options:0 error:&err];
						// create NSTextAttachment with that
						NSTextAttachment * imgAttachment = [[NSTextAttachment alloc] initWithFileWrapper:wrapper];
						// create NSAttributedString from that
						NSAttributedString * imgString = [NSAttributedString attributedStringWithAttachment:imgAttachment];
						// at last... insert it into the attributed string
						[worksText replaceCharactersInRange:styleRange withAttributedString:imgString];
						hasGraphics = YES;
					}
				}
			}
			else
			{
				[worksText setAttributes: [theContext makeFontAttribute: aStyle] range: styleRange];
			}
			styleRange.location += styleRange.length;
		}
	}

	RefVar rulers(GetFrameSlot(data, MakeSymbol("rulers")));
	if (IsArray(rulers))
	{
		NSRange styleRange = { 0, 0 };
		for (int i = 0, count = Length(rulers); i < count; i += 2)
		{
			NSMutableParagraphStyle * style = [[NSMutableParagraphStyle alloc] init];
			RefVar ruler(GetArraySlot(rulers, i+1));
			// map ruler.[justification, lineSpacing, indent, leftMargin, rightMargin, tabs] into NSMutableParagraphStyle
			RefVar item(GetFrameSlot(ruler, MakeSymbol("justification")));
			if (NOTNIL(item))
			{
				NSTextAlignment alignment = NSTextAlignmentNatural;
				if (EQ(item, SYMA(left)))
					alignment = NSTextAlignmentLeft;
				else if (EQ(item, SYMA(right)))
					alignment = NSTextAlignmentRight;
				else if (EQ(item, MakeSymbol("center")))
					alignment = NSTextAlignmentCenter;
				else if (EQ(item, MakeSymbol("justified")))
					alignment = NSTextAlignmentJustified;
				[style setAlignment: alignment];
			}
			if (NOTNIL(item = GetFrameSlot(ruler, MakeSymbol("lineSpacing"))))
				[style setLineHeightMultiple: RINT(item)];
			if (NOTNIL(item = GetFrameSlot(ruler, MakeSymbol("indent"))))
				[style setFirstLineHeadIndent: RINT(item)];
			if (NOTNIL(item = GetFrameSlot(ruler, MakeSymbol("leftMargin"))))
				[style setHeadIndent: RINT(item)];
			if (NOTNIL(item = GetFrameSlot(ruler, MakeSymbol("rightMargin"))))
				[style setTailIndent: -RINT(item)];
			if (NOTNIL(item = GetFrameSlot(ruler, SYMA(tabs))) && IsArray(item))
			{
				NSMutableArray * stops = [NSMutableArray arrayWithCapacity: Length(item)];
				FOREACH(item, tab)
					NSInteger tabLocation = RINT(GetFrameSlot(tab, MakeSymbol("value")));
					RefVar kind(GetFrameSlot(tab, MakeSymbol("kind")));
					// map tab frame -> txtab
					NSTextTabType tabType = NSLeftTabStopType;
					if (EQ(kind, SYMA(left)))
						tabType = NSLeftTabStopType;
					else if (EQ(kind, SYMA(right)))
						tabType = NSRightTabStopType;
					else if (EQ(kind, MakeSymbol("center")))
						tabType = NSCenterTabStopType;
					else if (EQ(kind, MakeSymbol("decimal")))
						tabType = NSDecimalTabStopType;
					NSTextTab * txtab = [[NSTextTab alloc] initWithType: tabType location: tabLocation];
					[stops addObject: txtab];
				END_FOREACH
				[style setTabStops: stops];
			}
			styleRange.length = RINT(GetArraySlot(rulers, i));
			[worksText addAttributes:@{ NSParagraphStyleAttributeName:style } range:styleRange];
			styleRange.location += styleRange.length;
		}
	}

	RefVar margins(GetFrameSlot(inEntry, MakeSymbol("margins")));
	if (NOTNIL(margins))
	{
	// set up docAttributes; margins, ViewMode & PaperSize?
		docAttributes = [NSMutableDictionary dictionaryWithCapacity: 4];
		[self addMargin: @"LeftMargin" from: margins slot: "left"];
		[self addMargin: @"RightMargin" from: margins slot: "right"];
		[self addMargin: @"TopMargin" from: margins slot: "top"];
		[self addMargin: @"BottomMargin" from: margins slot: "bottom"];

	//	can only do this if we set the PaperSize; NSRTFWriter raises exception otherwise
	//	int viewMode = 1;	// page view
	//	[docAttributes setObject: [NSValue value: &viewMode withObjCType: @encode(int)] forKey: @"ViewMode"];
	}
}


/*------------------------------------------------------------------------------
	Add a margin to the document’s attributes.
	Args:		inKey
				inFrame
				inTag
	Return:	--
------------------------------------------------------------------------------*/

- (void) addMargin: (NSString *) inKey from: (RefArg) inFrame slot: (const char *) inTag
{
	Ref value = GetFrameSlot(inFrame, MakeSymbol(inTag));
	if (ISINT(value))
		[docAttributes setObject: [NSNumber numberWithLong: RVALUE(value)] forKey: inKey];
}


/*------------------------------------------------------------------------------
	Convert NewtonWorks soup entry of class 'drawPaper to TIFF.
	Args:		inEntry			Works soup entry
	Return:	--
------------------------------------------------------------------------------*/

extern const Point	gZeroPoint;
extern void		InitDrawing(CGContextRef inContext, int inScreenHeight);
extern void		MungeBounds(RefArg inShape);
extern Rect *	ShapeBounds(RefArg inShape, Rect * outRect);
extern void		DrawShape(RefArg inShape, RefArg inStyle, Point inOffset);

- (void) translateDrawPaper: (RefArg) inEntry
{
//	REPprintf("\n");
//	PrintObject(inEntry, 0);

	RefVar data(GetFrameSlot(inEntry, MakeSymbol("saveData")));
	RefVar shapes(GetFrameSlot(data, MakeSymbol("shapes")));
	Rect fullBoundsRect;
	if (NOTNIL(shapes))
	{
#if defined(hasByteSwapping)
		MungeBounds(shapes);
#endif
		ShapeBounds(shapes, &fullBoundsRect);

		worksImage = [[NSImage alloc] initWithSize: NSMakeSize(fullBoundsRect.right+10,  fullBoundsRect.bottom+10)];
		[worksImage lockFocus];
 
		InitDrawing((CGContextRef) [[NSGraphicsContext currentContext] graphicsPort], fullBoundsRect.bottom+10);
		DrawShape(shapes, RA(NILREF), gZeroPoint);
 
		[worksImage unlockFocus];
	}
}


/*------------------------------------------------------------------------------
	Convert NewtonWorks soup entry of class '|QFNewt:donv| (Don Vollum’s Quick-
	Figure) to CSV.
	Based on _dataDefs.|QFNewt:donv|.TextScript
	The grid is 500 rows x 40 columns.
	Args:		inEntry			Works soup entry
	Return:	--
------------------------------------------------------------------------------*/

#define kNumOfRowsOnSheet   500
#define kNumOfColumnsOnSheet 40


NSString *
MakeCellText(unsigned index, unsigned * inCellPtr, char * inCellData)
{
	NSString * cellText = nil;
	unsigned dataOffset = inCellPtr[index];
#if defined(hasByteSwapping)
	dataOffset = BYTE_SWAP_LONG(dataOffset);
#endif

	if (dataOffset != 0)
	{
		unsigned dataLength = *(unsigned *)(inCellData + dataOffset);
#if defined(hasByteSwapping)
		dataLength = BYTE_SWAP_LONG(dataLength);
#endif
		CPtrPipe pipe;
		pipe.init(inCellData + dataOffset + 8, dataLength, NO, nil);
		RefVar cell(UnflattenRef(pipe));
		RefVar value(GetFrameSlot(cell, MakeSymbol("value")));
		RefVar formula(GetFrameSlot(cell, MakeSymbol("viewFormula")));
		if (IsString(formula))
			cellText = [NSString stringWithFormat: @"=%@", MakeNSString(formula)];
		else if (IsString(value))
			cellText = MakeNSString(value);
		else
			cellText = MakeNSString(SPrintObject(value));
	}
	return cellText;
}


- (void) translateSpreadsheet: (RefArg) inEntry
{
	// work out the sheet bounds
	unsigned cellRow, numOfRows = 0;
	unsigned cellCol, numOfCols = 0;
	unsigned index, limit = 0;
	RefVar cells(GetFrameSlot(inEntry, MakeSymbol("cells")));
	RefVar cellAddresses = GetFrameSlot(inEntry, MakeSymbol("cellAddresses"));
	unsigned numOfCellAddresses = Length(cellAddresses) / 4;	// will throw if nil
	Ref lastCellIndex = GetFrameSlot(inEntry, MakeSymbol("lastCellIndex"));
	if (NOTNIL(lastCellIndex))
	{
		limit = (unsigned)RINT(lastCellIndex) / 4;	// byte offset -> long offset
		if (limit > numOfCellAddresses)
			limit = numOfCellAddresses;
			numOfRows = limit / kNumOfColumnsOnSheet + 1;
			numOfCols = limit - ((limit / kNumOfColumnsOnSheet) * kNumOfColumnsOnSheet) + 1;
	}

	// iterate over cell data, stringing the text representation together
	NSCharacterSet * quotableSet = [NSCharacterSet characterSetWithCharactersInString: @",\n\""];
	worksCSV = [[NSMutableString alloc] init];
	index = 0;

	WITH_LOCKED_BINARY(cellAddresses, cellPtr)
	WITH_LOCKED_BINARY(cells, cellData)
	for (cellRow = 0; cellRow < numOfRows; cellRow++)
	{
		NSMutableString * rowText = [NSMutableString stringWithCapacity: 64];
		NSString * cellText;
		for (cellCol = 0; cellCol < numOfCols; cellCol++)
		{
			if (cellCol > 0)
				[rowText appendString: @","];
			if ((cellText = MakeCellText(index++, (unsigned *)cellPtr, (char *)cellData)))
			{
				NSRange qs = [cellText rangeOfCharacterFromSet: quotableSet];
				if (qs.location != NSNotFound)
				{
					cellText = [cellText stringByReplacingAllOccurrencesOfString: @"\"" withString: @"\"\""];
					cellText = [NSString stringWithFormat: @"\"%@\"", cellText];
				}
				[rowText appendString: cellText];
			}
		}
		for ( ; cellCol < kNumOfColumnsOnSheet; cellCol++)
			index++;
		if ([rowText length] >= numOfCols)
		{
			[rowText appendString: @"\n"];
			[worksCSV appendString: rowText];
		}
		else
			[worksCSV appendString: @"\n"];
	}
	END_WITH_LOCKED_BINARY(cells);
	END_WITH_LOCKED_BINARY(cellAddresses);
}

@end


/*------------------------------------------------------------------------------
	S t y l e   T r a n s l a t o r s
------------------------------------------------------------------------------*/

// map NSFont to packed font descriptor
Ref
MakeFontStyle(NSFont * inStyle)
{
	NSFontDescriptor * descriptor = [inStyle fontDescriptor];
	NSFontSymbolicTraits fontTraits = [descriptor symbolicTraits];
	NSString * fontName = [descriptor objectForKey: NSFontNameAttribute];

	unsigned fontFamily = tsSystem;
	unsigned fontFace = tsPlain;
	unsigned fontSize = [inStyle pointSize];

	if (fontName && [fontName isEqualToString: @"AppleCasual"])
		fontFamily = tsCasual;
	else if (fontName && [fontName isEqualToString: @"Geneva"])
		fontFamily = tsSimple;
	else
		switch (fontTraits & NSFontFamilyClassMask)
		{
			case NSFontOldStyleSerifsClass:
			case NSFontTransitionalSerifsClass:
			case NSFontModernSerifsClass:
			case NSFontClarendonSerifsClass:
			case NSFontSlabSerifsClass:
			case NSFontFreeformSerifsClass:
				fontFamily = tsFancy;
				break;
			case (uint32_t)NSFontSansSerifClass:
				fontFamily = tsSimple;
				break;
			case (uint32_t)NSFontScriptsClass:
				fontFamily = tsCasual;
				break;
		}

	if (fontTraits & NSFontItalicTrait) fontFace |= tsItalic;
	if (fontTraits & NSFontBoldTrait) fontFace |= tsBold;

	return MAKEINT(fontFamily + fontFace + tsSize(fontSize));
}


// map NSParagraphStyle into ruler.[justification, lineSpacing, indent, leftMargin, rightMargin, tabs]
Ref
MakeParaStyle(NSParagraphStyle * inStyle)
{
	RefVar ruler(AllocateFrame());
	RefVar tabs;
	RefVar item;
	float value;

	switch ([inStyle alignment])
	{
	case NSTextAlignmentLeft:
		item = SYMA(left);
		break;
	case NSTextAlignmentRight:
		item = SYMA(right);
		break;
	case NSTextAlignmentCenter:
		item = MakeSymbol("center");
		break;
	case NSTextAlignmentJustified:
		item = MakeSymbol("justified");
		break;
	default:
		item = SYMA(left);
		break;
	}
	SetFrameSlot(ruler, MakeSymbol("justification"), item);

	value = [inStyle lineHeightMultiple];
	if (value == 0.0)
		value = 1.0;
	SetFrameSlot(ruler, MakeSymbol("lineSpacing"), MAKEINT(value));

	value = [inStyle firstLineHeadIndent];
	SetFrameSlot(ruler, MakeSymbol("indent"), MAKEINT(value));

	value = [inStyle headIndent];
	SetFrameSlot(ruler, MakeSymbol("leftMargin"), MAKEINT(value));

	value = [inStyle tailIndent];
	// value > 0   =>  from the left
	// value <= 0  =>  from the right
	if (value > 0)
		value = 0;	// should work out (doc width - right margin) - (left margin + value)
	else if (value < 0)
		value = -value;
	SetFrameSlot(ruler, MakeSymbol("rightMargin"), MAKEINT(value));

	NSArray * tabStops = [inStyle tabStops];
	if (tabStops != nil && [tabStops count] > 0)
	{
		tabs = MakeArray(0);
		NSEnumerator * iter = [tabStops objectEnumerator];
		NSTextTab * txTab;
		while ((txTab = [iter nextObject]) != nil)
		{
			switch ([txTab tabStopType])
			{
			case NSLeftTabStopType:
				item = SYMA(left);
				break;
			case NSRightTabStopType:
				item = SYMA(right);
				break;
			case NSCenterTabStopType:
				item = MakeSymbol("center");
				break;
			case NSDecimalTabStopType:
				item = MakeSymbol("decimal");
				break;
			default:
				item = SYMA(left);
				break;
			}
			RefVar tab(AllocateFrame());
			SetFrameSlot(tab, MakeSymbol("kind"), item);
			SetFrameSlot(tab, MakeSymbol("value"), MAKEINT((int)[txTab location]));
			AddArraySlot(tabs, tab);
		}
	}
	SetFrameSlot(ruler, SYMA(tabs), tabs);

	return ruler;
}


/*------------------------------------------------------------------------------
	W o r k s F r o m T e x t
------------------------------------------------------------------------------*/

@interface WorksFromText : NCXTranslator
{
	BOOL isDone;
}
@end


@implementation WorksFromText

- (void) beginImport: (NSURL *) inURL context: (NCDocument *) inDocument
{
	[super beginImport: inURL context: inDocument];

	isDone = NO;
}


/*------------------------------------------------------------------------------
	Translate a plain text file to a Works soup entry of the form:

	{
		class: 'paper,
		title: "",
		summary: "",
		saveData: { txData: <>,
					 txText: <text:>,
					 styles: [n, { family:, face:, size: },…]
					 rulers: [n, { justification:, lineSpacing:, indent:, leftMargin:, rightMargin:, tabs: [{value:, kind:},…]},…]
					}
		],
		margins: { top:, left:, bottom:, right:, userTop:, userLeft:, userBottom:, userRight: },
		hiliteRange: { first:, last: },
		version: 1,
		timestamp: 12345678,
		labels: nil
	}

	This method will be called repeatedly until it returns NILREF.
	Args:		--
	Return:	Works soup entry
	To Do:	handle Unicode files (introduced by BOM 0xFEFF)
------------------------------------------------------------------------------*/

- (Ref) import
{
	RefVar theEntry;

	if (!isDone)
	{
		NSStringEncoding encoding;
		NSString * str = [[NSString alloc] initWithContentsOfURL: theURL usedEncoding: &encoding error: NULL];
		NSUInteger numOfChars = str.length;
		if (numOfChars > 0)
		{
#if defined(hasVBOs)
			NSUInteger actualSize = numOfChars * sizeof(UniChar);
			NSUInteger alignedSize = SUBPAGEALIGN((numOfChars+1) * sizeof(UniChar));

			RefVar store;
			RefVar stores(GetStores());
			if (IsArray(stores) && Length(stores) > 0)
				store = GetArraySlot(stores, 0);
			RefVar theText(FLBAllocCompressed(store, SYMA(text), MAKEINT(alignedSize), MakeStringFromCString("CLZStoreCompander"), RA(NILREF)));
			UniChar * tx = (UniChar *) BinaryData(theText);	// WITH_LOCKED_BINARY?
			[str getCharacters: tx];
			// NO LINEFEEDS!
			for (UniChar * p = tx; p < tx + numOfChars; p++)
			{
				if (*p == 0x0A)
#if defined(hasByteSwapping)
					*p = 0x0D00;
				else
					*p = BYTE_SWAP_SHORT(*p);
#else
					*p = 0x0D;
#endif
			}
			// zero remainder of buffer
			memset(tx + numOfChars, 0, alignedSize - actualSize);
			// flush to store -- compressed object will be streamed out
//			Compress((VAddr)tx);
#else
			RefVar theText(MakeString([str string]));
#endif

			RefVar styles(MakeArray(2));
			SetArraySlot(styles, 0, MAKEINT(numOfChars));
			SetArraySlot(styles, 1, MAKEINT(tsSimple + tsPlain + tsSize(9)));

			RefVar rulers(MakeArray(2));
			SetArraySlot(rulers, 0, MAKEINT(numOfChars));
			SetArraySlot(rulers, 1, MakeParaStyle([NSParagraphStyle defaultParagraphStyle]));

			RefVar theData = AllocateFrame();
#if defined(hasVBOs)
			SetFrameSlot(theData, MakeSymbol("txData"), [TXData txData: (unsigned int)numOfChars * sizeof(UniChar)]);
			SetFrameSlot(theData, MakeSymbol("txText"), theText);
#else
			SetFrameSlot(theData, MakeSymbol("text"), theText);
#endif
			SetFrameSlot(theData, MakeSymbol("styles"), styles);
			SetFrameSlot(theData, MakeSymbol("rulers"), rulers);

			NSString * title = [[theURL lastPathComponent] stringByDeletingPathExtension];
			NSString * summary = [str substringToIndex: MIN(numOfChars, 100)];
			// don’t allow newlines, etc in title
			summary = [summary stringByReplacingCharactersInSet: [NSCharacterSet controlCharacterSet] withString: @" "];

			theEntry = AllocateFrame();
			SetFrameSlot(theEntry, SYMA(class), MakeSymbol("paper"));
			SetFrameSlot(theEntry, MakeSymbol("saveData"), theData);
			SetFrameSlot(theEntry, SYMA(title), MakeString(title));
			SetFrameSlot(theEntry, MakeSymbol("summary"), MakeString(summary));
//			SetFrameSlot(theEntry, MakeSymbol("margins"), RA(NILREF));
//			SetFrameSlot(theEntry, MakeSymbol("hiliteRange"), RA(NILREF));
			SetFrameSlot(theEntry, SYMA(version), MAKEINT(1));
			SetFrameSlot(theEntry, SYMA(timestamp), MakeDate([NSDate date]));
		}
		isDone = YES;
	}

	return theEntry;
}

@end


/*------------------------------------------------------------------------------
	W o r k s F r o m R T F
------------------------------------------------------------------------------*/

@interface WorksFromRTF : NCXTranslator
{
	NSDictionary * docAttributes;
	BOOL isDone;
}
- (void) addMargin: (NSString *) inKey to: (RefArg) inFrame slot: (const char *) inTag;
@end


@implementation WorksFromRTF

- (void) beginImport: (NSURL *) inURL context: (NCDocument *) inDocument
{
	[super beginImport: inURL context: inDocument];

	isDone = NO;
}


- (Ref) import
{
	RefVar theEntry;

	if (!isDone)
	{
		NSError *__autoreleasing error = nil;
		NSDictionary * attr;
		NSAttributedString * str = [[NSAttributedString alloc] initWithURL:theURL options:NULL documentAttributes:&attr error:&error];
		docAttributes = [[NSDictionary alloc] initWithDictionary:attr];

		NSUInteger numOfChars = str.length;
		if (numOfChars > 0)
		{
#if defined(hasVBOs)
			NSUInteger actualSize = numOfChars * sizeof(UniChar);
			NSUInteger alignedSize = SUBPAGEALIGN((numOfChars+1) * sizeof(UniChar));

			RefVar store;
			RefVar stores(GetStores());
			if (IsArray(stores) && Length(stores) > 0)
				store = GetArraySlot(stores, 0);
			RefVar theText(FLBAllocCompressed(store, SYMA(text), MAKEINT(alignedSize), MakeStringFromCString("CLZStoreCompander"), RA(NILREF)));
			UniChar * tx = (UniChar *) BinaryData(theText);	// WITH_LOCKED_BINARY?
			[[str string] getCharacters: tx];
			// NO LINEFEEDS!
			for (UniChar * p = tx; p < tx + numOfChars; p++)
			{
				if (*p == 0x0A)
#if defined(hasByteSwapping)
					*p = 0x0D00;
				else
					*p = BYTE_SWAP_SHORT(*p);
#else
					*p = 0x0D;
#endif
			}
			// zero remainder of buffer
			memset(tx + numOfChars, 0, alignedSize - actualSize);
			// flush to store -- compressed object will be streamed out
//			Compress((VAddr)tx);
#else
			RefVar theText(MakeString([str string]));
#endif

			RefVar styles(MakeArray(0));
			NSRange totalRange = NSMakeRange(0, numOfChars);
			NSRange styleRange;
			NSUInteger index;
			for (index = 0; index < numOfChars; index = styleRange.location + styleRange.length)
			{
				NSFont * style = [str attribute: NSFontAttributeName atIndex: index longestEffectiveRange: &styleRange inRange: totalRange];
				AddArraySlot(styles, MAKEINT(styleRange.length));
				AddArraySlot(styles, MakeFontStyle(style));
				totalRange.location = styleRange.location + styleRange.length;
				totalRange.length = numOfChars - totalRange.location;
			}

			RefVar rulers(MakeArray(0));
			totalRange = NSMakeRange(0, numOfChars);
			for (index = 0; index < numOfChars; index = styleRange.location + styleRange.length)
			{
				NSParagraphStyle * style = [str attribute: NSParagraphStyleAttributeName atIndex: index longestEffectiveRange: &styleRange inRange: totalRange];
				AddArraySlot(rulers, MAKEINT(styleRange.length));
				AddArraySlot(rulers, MakeParaStyle(style));
				totalRange.location = styleRange.location + styleRange.length;
				totalRange.length = numOfChars - totalRange.location;
			}

			RefVar theData = AllocateFrame();
#if defined(hasVBOs)
			SetFrameSlot(theData, MakeSymbol("txData"), [TXData txData:(unsigned int)numOfChars * sizeof(UniChar)]);
			SetFrameSlot(theData, MakeSymbol("txText"), theText);
#else
			SetFrameSlot(theData, MakeSymbol("text"), theText);
#endif
			SetFrameSlot(theData, MakeSymbol("styles"), styles);
			SetFrameSlot(theData, MakeSymbol("rulers"), rulers);

			NSString * title = [[theURL lastPathComponent] stringByDeletingPathExtension];
			NSString * summary = [[str string] substringToIndex: MIN(numOfChars, 100)];
			// don’t allow newlines, etc in title
			summary = [summary stringByReplacingCharactersInSet: [NSCharacterSet controlCharacterSet] withString: @" "];

			RefVar margins(AllocateFrame());
			[self addMargin: @"LeftMargin" to: margins slot: "left"];
			[self addMargin: @"RightMargin" to: margins slot: "right"];
			[self addMargin: @"TopMargin" to: margins slot: "top"];
			[self addMargin: @"BottomMargin" to: margins slot: "bottom"];

			theEntry = AllocateFrame();
			SetFrameSlot(theEntry, SYMA(class), MakeSymbol("paper"));
			SetFrameSlot(theEntry, MakeSymbol("saveData"), theData);
			SetFrameSlot(theEntry, SYMA(title), MakeString(title));	// [NSString stringWithFormat: @"%@  • %@", title, summary]
			SetFrameSlot(theEntry, MakeSymbol("summary"), MakeString(summary));
			SetFrameSlot(theEntry, MakeSymbol("margins"), margins);
//			SetFrameSlot(theEntry, MakeSymbol("hiliteRange"), RA(NILREF));
			SetFrameSlot(theEntry, SYMA(version), MAKEINT(1));
			SetFrameSlot(theEntry, SYMA(timestamp), MakeDate([NSDate date]));
		}
		isDone = YES;
	}

	return theEntry;
}


/*------------------------------------------------------------------------------
	Add a margin from the document’s attributes.
	Args:		inKey
				inFrame
				inTag
	Return:	--
------------------------------------------------------------------------------*/

- (void) addMargin: (NSString *) inKey to: (RefArg) inFrame slot: (const char *) inTag
{
	NSNumber * value = [docAttributes objectForKey: inKey];
	if (value != nil)
		SetFrameSlot(inFrame, MakeSymbol(inTag), MAKEINT([value intValue]));
}


@end



#if 0
GetGlobals()._dataDefs.QFNewt:donv.TextScript

func(item, target)
begin
// set up some constant variables (sic)
local tabChar := SPrintObject($\t);
// work out the sheet bounds
local numOfRows := 0;
local numOfCols := 0;
local cellIndex := call NextCellIndex with (0, target);
while cellIndex do
	begin
	local cellRow := call RowFromIndex with (cellIndex);
	local cellCol := call ColFromIndex with (cellIndex);
	if cellRow > numOfRows then
		numOfRows := cellRow;
	if cellCol > numOfCols then
		numOfCols := cellCol;
	cellIndex := call NextCellIndex with ((cellIndex+1) * 4, target);
	end;

// create an array of cell data
local cellsPerRow := numOfCols + 1;
local cells := Array(cellsPerRow * (numOfRows + 1), nil);
local cellData := target.cells;
cellIndex := call NextCellIndex with (0, target);
while cellIndex do
	begin
	local cellAddr := ExtractLong(target.cellAddresses, cellIndex * 4);
	local cell := call MakeCell with (cellAddr, cellData);
	cellRow := call RowFromIndex with (cellIndex);
	cellCol := call ColFromIndex with (cellIndex);
	local formula := cell.viewFormula;
	if formula then
		cells[cellRow * cellsPerRow + cellCol] := "=" & formula
	else
		cells[cellRow * cellsPerRow + cellCol] := cell.value
	end;
	cellIndex := call NextCellIndex with ((cellIndex+1) * 4, target)
	end;

// string the cell data together
local theText := "";
for row := 0 to numOfRows do
	begin
	local rowText := "";
	local isRowEmpty := true;
	for col := 0 to numOfCols do
		begin
		local cellText := cells[row * cellsPerRow + col];
		if col > 0 then
			rowText := rowText & tabChar;
		if cellText then
			begin
			cellText := SPrintObject(cellText);
			if StrPos(cellText, tabChar, 0) then
				cellText := $" & cellText & $";
			rowText := rowText & cellText;
			isRowEmpty := nil
			end
		end;
	if not isRowEmpty then
		theText := theText & rowText;
	theText := theText & $\n
	end;

theText := Substr(theText, 1, StrLen(theText) - 1)
end


NextCellIndex := func(startingIndex, target)
begin
if not target.lastCellIndex then
	return;
if target.lastCellIndex > Length(target.cellAddresses) then
	target.lastCellIndex := Length(target.cellAddresses) - 4;
for index := startingIndex to target.lastCellIndex by 4 do
	begin
	local address := ExtractLong(target.cellAddresses, index);
	if address <> 0 then
		return index div 4;
	end;
nil
end


RowFromIndex := func(addr)
begin
addr div 40 + 1
end


ColFromIndex := func(addr)
begin
addr - (addr div 40) * 40 + 1
end


MakeCell := func(cellAddr, cellData)
begin
local cellDataSize := ExtractLong(cellData, cellAddr);
local objData := Clone("");
SetLength(objData, cellDataSize);
BinaryMunger(objData, 0, cellDataSize, cellData, cellAddr + 8, cellDataSize + 8);
local cell := Translate(objData, 'unflattener, nil, nil);
cell._proto := '{	cellName: nil,
						value: nil,
						formula: nil,
						viewFormula: nil,
						dependents: nil,
						format: nil,
						textFormat: nil,
						decPlaces: nil,
						row: nil,
						col: nil,
						protect: nil,
						dateFormat: nil };
cell
end


GetGlobals()._dataDefs.QFNewt:donv.TextScript.kFunc5

func(Arg1, Arg2) begin
local Local1, Local2, Local3, Local5, Local6, Local7, Local8, Local9, Local10;
Local2 := Arg1.cellName;
local dateSpecs := '[longDateStrSpec, abbrDateStrSpec, yearMonthDayStrSpec, yearMonthStrSpec, monthDayStrSpec, numericDateStrSpec, numericMDStrSpec];
if ClassOf(Arg1.value) = 'string
	then begin
		if BeginsWith(Arg1.value, "'")
			then begin
				Local3 := Substr(Arg1.value, 1, StrLen(Arg1.value) - 1);
				return Local3;
			end;
			else (Local3 := Arg1.value);
		if if Arg1.dateFormat
			then (Local5 := StringToDate(Local3))
			then begin
				if Arg1.dateFormat
					then (Local6 := Arg1.dateFormat);
				Local7 := @66 /* {#20} */.(dateSpecs[Local6]);
				if Local6 >= 5
					then (Local3 := ShortDateStr(Local5, Local7));
					else (Local3 := LongDateStr(Local5, Local7));
			end;
	end;
	else (if ClassOf(Arg1.value) = 'int or ClassOf(Arg1.value) = 'Real
		then begin
			Local8 := Arg1.value;
			if Arg1 and Arg1.format
				then (if Arg1.format > 0
					then begin
						if not Arg1.decPlaces
							then (dp := "2");
							else (dp := NumberStr(Arg1.decPlaces));
						if Arg1.format = 4
							then (Local9 := "e");
							else (Local9 := "f");
						Local1 := "%." & dp & Local9;
						if Arg1.format = 4
							then (Local3 := FormattedNumberStr(Local8, Local1));
							else (if Arg1.format = 2
								then begin
									if not Arg1.decPlaces
										then (Local10 := "%.f");
										else (Local10 := Local1);
									Local3 := FormattedNumberStr(Local8, Local10);
									StrReplace(Local3, GetLocale().numberFormat.minusPrefix, "", nil);
								end;
								else (if Arg1.format = 3 or Arg1.format = 6
									then begin
										Local3 := GetLocale().numberFormat.currencyPrefix & FormattedNumberStr(Local8, Local1) & GetLocale().numberFormat.currencySuffix;
										if Local8 < 0 and Arg1.format = 6
											then begin
												Local3 := "(" & Local3 & ")";
												StrReplace(Local3, GetLocale().numberFormat.minusPrefix, "", nil);
											end;
									end;
									else (if Arg1.format = 1
										then (Local3 := FormattedNumberStr(Local8 * 100, Local1) & "%");
										else (if Arg1.format = 5
											then begin
												if not Arg1.decPlaces
													then (Local1 := "%.f");
												Local3 := FormattedNumberStr(Local8, Local1);
												if Local8 < 0
													then (Local3 := "(" & Local3 & ")");
												StrReplace(Local3, GetLocale().numberFormat.minusPrefix, "", nil);
											end))));
					end);
			if not Local3
				then begin
					if not Arg1.decPlaces
						then (Local3 := NumberStr(Local8));
						else (Local3 := FormattedNumberStr(Local8, "%." & NumberStr(Arg1.decPlaces) & "f"));
					StrReplace(Local3, GetLocale().numberFormat.groupSepStr, "", nil);
				end;
		end);
if not Local3
	then (Local3 := SPrintObject(Arg1.value));
Local3
end

#endif
