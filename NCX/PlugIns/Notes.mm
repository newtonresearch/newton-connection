/*
	File:		Notes.mm

	Contains:   Newton Notes import/export functions.

	Written by: Newton Research Group, 2006.
*/

#import "NCXTranslator.h"
#import "PlugInUtilities.h"

extern void		UnionRect(const Rect * src1, const Rect * src2, Rect * dstRect);
//extern BOOL		EmptyRect(const Rect * rect);

BOOL
IsEmptyRect(const Rect * rect)
{
	return rect->right <= rect->left || rect->bottom <= rect->top;
}


/*------------------------------------------------------------------------------
	N o t e T o R T F
------------------------------------------------------------------------------*/

@interface NoteToRTF : NCXTranslator
{
	NSDictionary * defaultStyle;
	NSDictionary * bulletStyle;
	BOOL hasImages;
}
- (NSAttributedString *)translate:(RefArg)inEntry;
- (NSAttributedString *)translateText:(RefArg)inEntry;
- (NSAttributedString *)translateList:(RefArg)inEntry;
- (NSAttributedString *)translateGraphPaper:(RefArg)inEntry;
- (NSAttributedString *)renderShapes:(RefArg)inData;
@end


@implementation NoteToRTF

- (id) init
{
	if (self = [super init])
	{
		defaultStyle = @{ NSFontAttributeName:[NSFont fontWithName:@"HelveticaNeue" size: 12.0] };
		bulletStyle = @{ NSFontAttributeName:[NSFont fontWithName: @"AppleSymbols" size: 16.0] };
	}
	return self;
}


/*------------------------------------------------------------------------------
	Convert Notes soup entry to RTF file.
	Args:		inEntry			Notes soup entry
				inDocument		Newton context
				inPath			base file URL
	Return:	name of file created
------------------------------------------------------------------------------*/

- (NSString *) export: (RefArg) inEntry
{
	NSString * fileWritten = nil;
	NSAttributedString * noteText = [self translate:inEntry];
	if (noteText)
	{
		if (hasImages)
		{
			NSData * rtfdData = [noteText RTFDFromRange:NSMakeRange(0, noteText.length) documentAttributes:@{NSDocumentTypeDocumentAttribute:NSRTFDTextDocumentType}];
			fileWritten = [self write:rtfdData toFile:[self makeFilename:inEntry] extension:@"rtfd"];
//	Could draw into PDF...
//			NSView * renderView;		// needs to be declared somewhere
//			NSData * pdfData = [renderView dataWithPDFInsideRect: [renderView bounds]];
//			fileWritten = [self write:pdfData toFile:[self makeFilename:inEntry] extension:@"pdf"];
		}
		else
		{
		//	could do something with documentAttributes
			NSData * rtfData = [noteText RTFFromRange:NSMakeRange(0, noteText.length) documentAttributes:@{NSDocumentTypeDocumentAttribute:NSRTFTextDocumentType}];
			fileWritten = [self write:rtfData toFile:[self makeFilename:inEntry] extension:@"rtf"];
		}
	}
	return fileWritten;
}


- (NSAttributedString *)translate:(RefArg)inEntry
{
	hasImages = NO;
	NSAttributedString * noteText = nil;
	newton_try
	{
		RefVar entryClass(ClassOf(inEntry));
		if (EQRef(entryClass, MakeSymbol("paperroll")))
			noteText = [self translateText: inEntry];
		else if (EQRef(entryClass, MakeSymbol("list"))
			  ||  EQRef(entryClass, MakeSymbol("checklist")))
			noteText = [self translateList: inEntry];
		else if (EQRef(entryClass, MakeSymbol("graphPaper:RSM")))
			noteText = [self translateGraphPaper: inEntry];
	}
	newton_catch_all
	{}
	end_try;
	return noteText;
}


/*------------------------------------------------------------------------------
	Convert Notes soup entry of class 'paperroll to RTF.
	Args:		inEntry			Notes soup entry
	Return:	auto-released NSAttributedString
------------------------------------------------------------------------------*/

extern const Point	gZeroPoint;
extern void		InitDrawing(CGContextRef inContext, int inScreenHeight);
extern bool		FromObject(RefArg inObj, Rect * outBounds);
extern void		DrawShape(RefArg inShape, RefArg inStyle, Point inOffset);
//extern void		DrawPicture(RefArg inIcon, const Rect * inFrame, ULong inJustify);
extern void		DrawPolygon(char * inData, short inX, short inY);
extern void		InkDrawInRect(RefArg inkObj, size_t inPenSize, Rect * inOriginalBounds, Rect * inBounds, bool inLive);
extern float	LineWidth(void);


void
SetEmptyRect(Rect * r)
{
	r->left = 0;
	r->top = 0;
	r->right = 0;
	r->bottom = 0;
}

inline int
RectGetWidth(const Rect * inRect)
{ return inRect->right - inRect->left; }

inline int
RectGetHeight(const Rect * inRect)
{ return inRect->bottom - inRect->top; }


/*------------------------------------------------------------------------------
	Start with a zero view size.
	Iterate over paras in data
		UnionRect view size w/ viewBounds
		if class is 'ink, 'pict, 'poly then we’ll be rendering graphics into a view
	if rendering then create renderView
	Iterate over paras in data
		if class is 'ink, 'pict, 'poly then draw into renderView

	How are we drawing text into the renderView?
------------------------------------------------------------------------------*/


- (NSAttributedString *) translateText: (RefArg) inEntry
{
	RefVar data(GetFrameSlot(inEntry, SYMA(data)));
	if (NOTNIL(data))
		return [self renderShapes:data];
	return nil;
}


/*------------------------------------------------------------------------------
	Convert Notes soup entry of class 'list or 'checklist to RTF.
	Args:		inEntry			Notes soup entry
	Return:	auto-released NSAttributedString
------------------------------------------------------------------------------*/

- (NSAttributedString *) translateList: (RefArg) inEntry
{
	NSMutableAttributedString * noteText = [[NSMutableAttributedString alloc] initWithString: @""];

	RefVar data(GetFrameSlot(inEntry, MakeSymbol("topics")));
	if (NOTNIL(data))
	{
		BOOL isPlainList = EQRef(ClassOf(inEntry), MakeSymbol("list"));
		RefVar text;
		FOREACH(data, topic)
		// extract text
			if (NOTNIL(text = GetFrameSlot(topic, SYMA(text))))
			{
				int indent = 1;
				RefVar level(GetFrameSlot(topic, MakeSymbol("level")));
				if (NOTNIL(level))
					indent = (int)RVALUE(level);

				NSString * bullet;
				if (isPlainList)
					bullet = [NSString stringWithUTF8String: "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\u25E6 "];
				else
				{
					if (ISNIL(GetFrameSlot(topic, MakeSymbol("mtgDone"))))
						bullet = [NSString stringWithUTF8String: "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\u2610\u25E6 "];
					else
						bullet = [NSString stringWithUTF8String: "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\u2611\u25E6 "];
				}
				NSAttributedString * bulletStr = [[NSAttributedString alloc] initWithString: [bullet substringFromIndex: indent > 16 ? 16 : 17 - indent] attributes: bulletStyle];
				[noteText appendAttributedString: bulletStr];

				NSString * topicText = [NSString stringWithFormat: @"%@\n", MakeNSString(text)];
				NSDictionary * topicStyle = defaultStyle;
				NSMutableAttributedString * topicStr = [[NSMutableAttributedString alloc] initWithString: topicText attributes: topicStyle];
				RefVar styles(GetFrameSlot(topic, SYMA(styles)));
				if (IsArray(styles))
				{
					NSRange styleRange = { 0, 0 };
					int i, count = Length(styles);
					for (i = 0; i < count; i += 2)
					{
						styleRange.length = RINT(GetArraySlot(styles, i));
						topicStyle = [theContext makeFontAttribute: GetArraySlot(styles, i+1)];
						[topicStr setAttributes: topicStyle range: styleRange];
						styleRange.location += styleRange.length;
					}
				}
				[noteText appendAttributedString: topicStr];
			}
		END_FOREACH
	}

	return noteText;
}


/*------------------------------------------------------------------------------
	Convert Notes soup entry of class '|graphPaper:RSM| to RTF.
	Do it like -translateText: for 'paperroll.

	Start with a zero view size.
	Iterate over paras in |graphPaper:RSM|.editView
		UnionRect view size w/ viewBounds
		if class is 'ink, 'pict, 'poly then we’ll be rendering graphics into a view
	if rendering then create renderView
	Iterate over paras in |graphPaper:RSM|.editView again
		if class is 'ink, 'pict, 'poly then draw into renderView

	Args:		inEntry			Notes soup entry
	Return:	auto-released NSAttributedString
------------------------------------------------------------------------------*/

- (NSAttributedString *) translateGraphPaper: (RefArg) inEntry
{
	RefVar data(GetFrameSlot(inEntry, MakeSymbol("graphPaper:RSM")));
	if (NOTNIL(data))
		data = GetFrameSlot(data, MakeSymbol("editView"));
	if (NOTNIL(data))
		return [self renderShapes:data];
	return nil;
}


- (NSAttributedString *) renderShapes: (RefArg) inData
{
	NSMutableAttributedString * noteText = [[NSMutableAttributedString alloc] initWithString: @""];
	NSImage * renderImage = nil;

	if (NOTNIL(inData))
	{
		RefVar text, ink;
		Rect renderBounds, inkBounds;

		SetEmptyRect(&renderBounds);
		FOREACH(inData, para)
		// open bounds rect to enclose all graphics
//PrintObject(para, 0);
			ink = GetFrameSlot(para, SYMA(viewStationery));
			if (NOTNIL(GetFrameSlot(para, SYMA(ink)))
			||  EQ(ink, MakeSymbol("pict"))
			||  EQ(ink, MakeSymbol("poly")))
			{
				if (NOTNIL(ink = GetFrameSlot(para, SYMA(viewBounds)))
				&&  FromObject(ink, &inkBounds))
				{
					UnionRect(&renderBounds, &inkBounds, &renderBounds);
				}
			}
		END_FOREACH

		if (!IsEmptyRect(&renderBounds))
		{
			// create NSImage large enough to enclose all graphics
			renderImage = [[NSImage alloc] initWithSize: NSMakeSize(renderBounds.right+2, renderBounds.bottom+2)];
			[renderImage lockFocus];
			InitDrawing((CGContextRef) [[NSGraphicsContext currentContext] graphicsPort], renderBounds.bottom+2);
		}

		FOREACH(inData, para)
		// extract text
			if (EQ(GetFrameSlot(para, SYMA(viewStationery)), MakeSymbol("para"))
			&&  NOTNIL(text = GetFrameSlot(para, SYMA(text))))
			{
				NSString * paraText = [NSString stringWithFormat: @"%@\n", MakeNSString(text)];
				NSDictionary * paraStyle = [theContext makeFontAttribute: GetFrameSlot(para, SYMA(viewFont))];
				NSMutableAttributedString * paraStr = [[NSMutableAttributedString alloc] initWithString: paraText attributes: paraStyle];
				RefVar theStyle, styles(GetFrameSlot(para, SYMA(styles)));
				if (IsArray(styles))
				{
					NSRange styleRange = { 0, 0 };
					int i, count = Length(styles);
					for (i = 0; i < count; i += 2)
					{
						styleRange.length = RINT(GetArraySlot(styles, i));
						theStyle = GetArraySlot(styles, i+1);
						paraStyle = [theContext makeFontAttribute: theStyle];
						if (EQ(ClassOf(theStyle), SYMA(inkWord)))
						{
							// the style slot is <inkWord: x> corresponding to a kInkChar in the text
							// so we need to render that to get ink text
							// for the time being we’ll just acknowledge that it’s ink
							[paraStr replaceCharactersInRange: styleRange withString: @"-ink-"];
							styleRange.length += 4;
						}
						[paraStr setAttributes: paraStyle range: styleRange];
						styleRange.location += styleRange.length;
					}
				}
				[noteText appendAttributedString: paraStr];
			}

		// extract ink
			else if (NOTNIL(ink = GetFrameSlot(para, SYMA(ink))))	// ink is 'ink2
			{
				FromObject(GetFrameSlot(para, SYMA(viewBounds)), &inkBounds);
				InkDrawInRect(ink, 2, &inkBounds, &inkBounds, NO);
			}

		// extract image
			else if (EQ(GetFrameSlot(para, SYMA(viewStationery)), MakeSymbol("pict"))
			&&  NOTNIL(ink = GetFrameSlot(para, SYMA(icon))))
			{
				FromObject(GetFrameSlot(para, SYMA(viewBounds)), &inkBounds);
				DrawShape(ink, RA(NILREF), gZeroPoint);
			}

		// extract polygon
			else if (EQ(GetFrameSlot(para, SYMA(viewStationery)), MakeSymbol("poly"))
			&&  NOTNIL(ink = GetFrameSlot(para, SYMA(points))))	// ink is a 'polygonShape
			{
				FromObject(GetFrameSlot(para, SYMA(viewBounds)), &inkBounds);
				CDataPtr inkData(ink);
				DrawPolygon((Ptr)inkData, inkBounds.left, inkBounds.top);
			}

		END_FOREACH

		if (renderImage)
		{
			[renderImage unlockFocus];

			// need to add renderImage to the RTFD
			NSURL * inkURL = ApplicationSupportFile(@"image.tiff");	// [self makePathFrom: inEntry withExtension: @"tif"]
			NSData * tiffData = [renderImage TIFFRepresentationUsingCompression: NSTIFFCompressionLZW factor: 1.0];
			if (tiffData && [tiffData writeToURL:inkURL options:0 error:nil])
			{
// make a NSFileWrapper for the image file
				NSFileWrapper * wrapper = [[NSFileWrapper alloc] initWithURL: inkURL options: 0 error: NULL];
// make a NSTextAttachment with that file wrapper
				NSTextAttachment * attachment = [[NSTextAttachment alloc] initWithFileWrapper: wrapper];
// make a NSAttributedString from that NSTextAttachment
				NSAttributedString * inkStr = [NSAttributedString attributedStringWithAttachment: attachment];
// add that string to our text
				[noteText appendAttributedString: inkStr];
				hasImages = YES;
			}
		}
	}

	return noteText;
}

@end


/*------------------------------------------------------------------------------
	N o t e F r o m T e x t
------------------------------------------------------------------------------*/

@interface NoteFromText : NCXTranslator
{
	FILE * sourceFile;
	NSRange range;
	int index;
}
@end


@implementation NoteFromText

- (void) beginImport: (NSURL *) inURL context: (NCDocument *) inDocument
{
	[super beginImport: inURL context: inDocument];

	sourceFile = fopen(inURL.fileSystemRepresentation, "r");
	fseek(sourceFile, 0, SEEK_END);
	range = NSMakeRange(0, ftell(sourceFile));
	index = 0;
}


/*------------------------------------------------------------------------------
	Translate a plain text file to a Notes soup entry of the form:

	{													// @382
		class: 'paperroll,
		viewStationery: 'paperroll,
		title: "",
		data: [ { text: "",						// @267
				//	 tabs: nil,  styles: nil,
					 viewBounds: { top: left: bottom: right: },
					 viewStationery: 'para
					}
		],
		height: 100,
		timeStamp: 12345678,
		labels: nil
	}

	Each returned entry is a note containing about 4K of text.
	This method will be called repeatedly until it returns NILREF.
	Args:		--
	Return:	Notes soup entry
	To Do:	handle Unicode files (introduced by BOM 0xFEFF)
------------------------------------------------------------------------------*/

- (Ref) import
{
	RefVar theEntry;

	if (range.length > 0)
	{
		char * txt;
		NSRange chunk = range;
		BOOL isPartial = chunk.length > 4096;
		if (isPartial)
			chunk.length = 4096;

		fseek(sourceFile, chunk.location, SEEK_SET);
		txt = (char *) malloc(chunk.length+1);
		fread(txt, 1, chunk.length, sourceFile);
		if (isPartial)
		{
		// break on words - look back from end of txt for whitespace
			NSUInteger i;
			for (i = chunk.length; !isspace(txt[i]) && i > 0; i--)
				;
			if (i > 0)
				chunk.length = i;
		}
		txt[chunk.length] = 0;
		// NO LINEFEEDS!
		for (char * p = txt; p < txt + chunk.length; p++)
			if (*p == 0x0A)
				*p = 0x0D;
		range.location += chunk.length;
		range.length -= chunk.length;
		index++;

		RefVar theText(MakeStringFromCString(txt));
		free(txt);

		Rect textBounds;
		textBounds.left = 5;
		textBounds.right = 320 - 5;	// do we know this from the NewtonInfo?
		textBounds.top = 5;
		textBounds.bottom = 505;

		RefVar thePara(Clone(MAKEMAGICPTR(267)));
		SetFrameSlot(thePara, SYMA(text), theText);
		SetFrameSlot(thePara, SYMA(viewBounds), ToObject(&textBounds));
//		SetFrameSlot(thePara, SYMA(viewFont), [theContext userFontRef]);

		RefVar theData(MakeArray(1));
		SetArraySlot(theData, 0, thePara);

		NSString * title = [[theURL lastPathComponent] stringByDeletingPathExtension];
		if (index > 1)
			title = [title stringByAppendingFormat: @" (%d)", index];

		theEntry = Clone(MAKEMAGICPTR(382));
		SetFrameSlot(theEntry, SYMA(data), theData);
		SetFrameSlot(theEntry, SYMA(title), MakeString(title));
		SetFrameSlot(theEntry, SYMA(height), MAKEINT(500));
		SetFrameSlot(theEntry, SYMA(timestamp), MakeDate([NSDate date]));
	}

	return theEntry;
}


- (void) importDone
{
	fclose(sourceFile);
}

@end


/*------------------------------------------------------------------------------
	N o t e F r o m R T F
------------------------------------------------------------------------------*/

@interface NoteFromRTF : NCXTranslator
{
	NSAttributedString * sourceStr;
	NSRange range;
	int index;
}
@end


@implementation NoteFromRTF

- (void) beginImport: (NSURL *) inURL context: (NCDocument *) inDocument
{
	[super beginImport: inURL context: inDocument];

	NSError * __autoreleasing error;
	sourceStr = [[NSAttributedString alloc] initWithURL:inURL options:0 documentAttributes:NULL error:&error];
	range = NSMakeRange(0, [sourceStr length]);
	index = 0;
}


/*------------------------------------------------------------------------------
	Translate a RTF file to a Notes soup entry.
		break up paragraphs by style?
		dict = [attrStr attributesAtIndex: index longestEffectiveRange: &aRange inRange: rangeLimit];
	Since a Newton note is limited in size, this method can be called many
	times, passing a context structure, until it returns NILREF.
	Each returned entry is a note containing about 4K of text.
	Args:		--
	Return:	Note
------------------------------------------------------------------------------*/

- (Ref) import
{
	RefVar theEntry;

	if (range.length > 0)
	{
		NSRange chunk = range;
		BOOL isPartial = chunk.length > 4096;
		if (isPartial)
			chunk.length = 4096;

		NSAttributedString * attrStr = [sourceStr attributedSubstringFromRange: chunk];
		NSString * str = [attrStr string];

		if (isPartial)
		{
		// break on words - look back from end of str for whitespace
			NSRange ws = [str rangeOfCharacterFromSet: [NSCharacterSet whitespaceAndNewlineCharacterSet] options: NSBackwardsSearch];
			if (ws.location != NSNotFound)
				chunk.length = ws.location + ws.length;
		}

		range.location += chunk.length;
		range.length -= chunk.length;
		index++;

		RefVar theText(MakeString(str));

		Rect textBounds;
		textBounds.left = 5;
		textBounds.right = 320 - 5;	// do we know this from the NewtonInfo?
		textBounds.top = 5;
		textBounds.bottom = 505;

		RefVar thePara(Clone(MAKEMAGICPTR(267)));
		SetFrameSlot(thePara, SYMA(text), theText);
		SetFrameSlot(thePara, SYMA(viewBounds), ToObject(&textBounds));
//		SetFrameSlot(thePara, SYMA(viewFont), [theContext userFontRef]);

		RefVar theData(MakeArray(1));
		SetArraySlot(theData, 0, thePara);

		NSString * title = [[theURL lastPathComponent] stringByDeletingPathExtension];
		if (index > 1)
			title = [title stringByAppendingFormat: @" (%d)", index];

		theEntry = Clone(MAKEMAGICPTR(382));
		SetFrameSlot(theEntry, SYMA(data), theData);
		SetFrameSlot(theEntry, SYMA(title), MakeString(title));
		SetFrameSlot(theEntry, SYMA(height), MAKEINT(500));
		SetFrameSlot(theEntry, SYMA(timestamp), MakeDate([NSDate date]));
	}

	return theEntry;
}


- (void) importDone
{
	sourceStr = nil;
}

@end
