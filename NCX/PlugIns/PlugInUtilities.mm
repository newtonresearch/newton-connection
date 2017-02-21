/*
	File:		PlugInUtilities.mm

	Contains:	Utility functions for the NCX app plug-ins.

	Written by:	Newton Research Group, 2005.
*/

#import "PlugInUtilities.h"
#import "NSString-Extensions.h"

extern Ref MakeStringOfLength(const UniChar * str, ArrayIndex numChars);


/*------------------------------------------------------------------------------
	U t i l i t i e s
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
	Make a NextStep string from a NewtonScript string.
	Args:		inStr			a NewtonScript string
	Return:	an autoreleased NSString
------------------------------------------------------------------------------*/

NSString *
MakeNSString(RefArg inStr)
{
	if (IsString(inStr))
		return [NSString stringWithCharacters: GetUString(inStr)
												 length: (Length(inStr) - sizeof(UniChar))/sizeof(UniChar)];
	return nil;
}


/*------------------------------------------------------------------------------
	Make a NewtonScript string from a NextStep string.
	Args:		inStr			an NSString
	Return:	a NewtonScript string
------------------------------------------------------------------------------*/

Ref
MakeString(NSString * inStr)
{
	RefVar s;
	UniChar buf[128];
	UniChar * str = buf;
	size_t strLen = inStr.length;
	if (strLen > 128)
		str = (UniChar *) malloc(strLen*sizeof(UniChar));
	[inStr getCharacters: str];
	// NO LINEFEEDS!
	for (UniChar * p = str; p < str + strLen; p++)
		if (*p == 0x0A)
			*p = 0x0D;
	s = MakeStringOfLength(str, (ArrayIndex)strLen);
	if (str != buf)
		free(str);
	return s;
}


/*------------------------------------------------------------------------------
	Make a NextStep date from a NewtonScript date.
	A NewtonScript date is classically the number of minutes since 1904.
	However…
	Newton measures time in seconds since 1993, but 2^29 seconds (signed NewtonScript
	integer) overflow in 2010.
	Avi Drissman’s fix for this is rebase seconds on a hexade (16 years):
		1993, 2009, 2025…
	Args:		inDate			a NewtonScript date
	Return:	an autoreleased NSDate
------------------------------------------------------------------------------*/

#define kMinutes1904to1970 34714080

NSDate *
MakeNSDate(RefArg inDate)
{
	if (ISINT(inDate)) {
		NSTimeInterval interval = RVALUE(inDate);
		return [NSDate dateWithTimeIntervalSince1970: (interval - kMinutes1904to1970)*60];
	}
	return nil;
}


NSDateComponents *
MakeNSDateComponents(RefArg inDate)
{
	NSDate * date = MakeNSDate(inDate);
	if (date) {
		NSCalendar * cal = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
		return [cal components:NSCalendarUnitYear|NSCalendarUnitMonth|NSCalendarUnitDay fromDate:date];
	}
	return nil;
}


/*------------------------------------------------------------------------------
	Make a NewtonScript date (number of minutes since 1904) from a NextStep date.
	Args:		inDate			an NSDate
	Return:	a NewtonScript integer
------------------------------------------------------------------------------*/

Ref
MakeDate(NSDate * inDate)
{
	NSTimeInterval interval = [inDate timeIntervalSince1970]/60;	// seconds -> minutes
	return MAKEINT(kMinutes1904to1970 + interval);
}


/*------------------------------------------------------------------------------
	Make a font from a Newton font spec.
	Args:		inFontSpec		a font spec
	Return:	an autoreleased NSFont
------------------------------------------------------------------------------*/

NSFont *
MakeNSFont(RefArg inFontSpec)
{
	NSString * fontName = @"Helvetica Neue";
	NSFontTraitMask fontTraits = 0;
	unsigned int fontFace = 0;
	float fontSize = 12.0;

	if (ISINT(inFontSpec))
	{
		unsigned int packedFont = (unsigned int)RVALUE(inFontSpec);
		unsigned int fontFamily = (packedFont & tsFamilyMask) >> tsFamilyShift;
		fontFace = (packedFont & tsFaceMask) >> tsFaceShift;
		fontSize = (packedFont & tsSizeMask) >> tsSizeShift;
		switch (fontFamily)
		{
		case 0:	// system
			fontName = @"Helvetica Neue";
			break;
		case 1:	// fancy
			fontName = @"Hoefler Text";
			break;
		case 2:	// simple
			fontName = @"Geneva";
			break;
		case 3:	// handwriting
			fontName = @"Casual";
			break;
		}
	}
	else if (IsInkWord(inFontSpec))
	{
/*		InkWordInfo info;
		GetInkWordInfo(inFontSpec, &info);
		fontFamily = inFontSpec;
		fontSize = info.x20;
		fontFace = info.x10;
*/	}
	else if (IsFrame(inFontSpec))
	{
		fontName = [NSString stringWithUTF8String: SymbolName(GetFrameSlot(inFontSpec, SYMA(family)))];
		fontFace = (unsigned int)RINT(GetFrameSlot(inFontSpec, SYMA(face)));
		fontSize = RINT(GetFrameSlot(inFontSpec, SYMA(size)));
	}
	if (fontFace & kBoldFace) fontTraits |= NSBoldFontMask;
	if (fontFace & kItalicFace) fontTraits |= NSItalicFontMask;
	id font = [[NSFontManager sharedFontManager] fontWithFamily: fontName traits: fontTraits weight: 5 size: fontSize];

	if (font == nil)
		font = [NSFont fontWithName: @"HelveticaNeue" size: fontSize];

	return font;
}


/*------------------------------------------------------------------------------
	Return the URL of a file in our application support folder.
	Args:		--
	Return:	an autoreleased NSURL
------------------------------------------------------------------------------*/

NSURL *
ApplicationSupportFolder(void)
{
	NSURL * baseURL = [NSFileManager.defaultManager URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:NULL];
	NSURL * appFolder = [baseURL URLByAppendingPathComponent:@"Newton Connection" isDirectory:YES];
	// if folder doesn’t exist, create it
	[NSFileManager.defaultManager createDirectoryAtURL:appFolder withIntermediateDirectories:NO attributes:nil error:NULL];
	return appFolder;
}

NSURL *
ApplicationSupportFile(NSString * inFilename)
{
	return [ApplicationSupportFolder() URLByAppendingPathComponent:inFilename];
}


/*------------------------------------------------------------------------------
	Return the URL of the log file in the user’s logs folder.
	Args:		--
	Return:	an autoreleased NSURL
------------------------------------------------------------------------------*/

NSURL *
ApplicationLogFile(void)
{
	// find /Library
	NSURL * url = [NSFileManager.defaultManager URLForDirectory:NSLibraryDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:NO error:NULL];
	// cd /Library/Logs
	url = [url URLByAppendingPathComponent:@"Logs" isDirectory:YES];
	// if folder doesn’t exist, create it
//	[NSFileManager.defaultManager createDirectoryAtURL:url withIntermediateDirectories:NO attributes:nil error:NULL];
	// create /Library/Logs/NewtonConnection.log
	return [url URLByAppendingPathComponent:@"NewtonConnection.log"];
}
