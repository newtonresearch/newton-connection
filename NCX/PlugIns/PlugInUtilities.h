/*
	File:		PlugInUtilities.h

	Contains:	Utility functions for NCX plugins.

	Written by:	Newton Research Group, 2006.
*/

#import <Cocoa/Cocoa.h>
#import "NewtonKit.h"

// from Newton.framework

#define tsFamilyShift		 0
#define tsSizeShift			10
#define tsFaceShift			20
#define tsFamilyMask			0x000003FF
#define tsSizeMask			0x000FFC00
#define tsFaceMask			0x3FF00000

#define tsCasual				(3 << tsFamilyShift)
#define tsSimple				(2 << tsFamilyShift)
#define tsFancy				(1 << tsFamilyShift)
#define tsSystem				(0 << tsFamilyShift)

#define tsSize(num)			((num) << tsSizeShift)

#define kPlainFace			 0
#define kBoldFace				(1 << 0)
#define kItalicFace			(1 << 1)
#define kUnderlineFace		(1 << 2)
#define kOutlineFace			(1 << 3)
#define kSuperScriptFace	(1 << 7)
#define kSubScriptFace		(1 << 8)
#define kUndefinedFace		(1 << 9)

#define tsPlain				 0
#define tsBold					(kBoldFace << tsFaceShift)
#define tsItalic				(kItalicFace << tsFaceShift)
#define tsUnderline			(kUnderlineFace << tsFaceShift)
#define tsOutline				(kOutlineFace << tsFaceShift)
#define tsSuperScript		(kSuperScriptFace << tsFaceShift)
#define tsSubScript			(kSubScriptFace << tsFaceShift)
#define tsUndefinedFace		(kUndefinedFace << tsFaceShift)

extern bool			StrEmpty(RefArg inStr);
extern "C" bool	IsWhiteSpace(UniChar c);

extern Ref			SetBoundsRect(RefArg ioFrame, const Rect * inBounds);
extern Ref			ToObject(const Rect * inBounds);

extern NSString *	MakeNSString(RefArg inStr);
extern Ref			MakeString(NSString * inStr);
extern NSDate *	MakeNSDate(RefArg inDate);
extern NSDateComponents *	MakeNSDateComponents(RefArg inDate);
extern Ref			MakeDate(NSDate * inDate);
extern NSFont *	MakeNSFont(RefArg inFontSpec);

extern bool			IsInkWord(RefArg inObj);

extern NSURL *		ApplicationSupportFolder(void);
extern NSURL *		ApplicationSupportFile(NSString * inFilename);
extern NSURL *		ApplicationLogFile(void);

