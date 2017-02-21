/*
	File:		Utilities.mm

	Contains:	Utility functions for the NCX app.

	Written by:	Newton Research Group, 2005.
*/

#import "Utilities.h"
#import "Newton/PackageParts.h"
#import "Newton/OSErrors.h"


/* -----------------------------------------------------------------------------
	U t i l i t i e s
----------------------------------------------------------------------------- */

/* -----------------------------------------------------------------------------
	Determine whether a store is internal (or on a card).
	Args:		inStore			a store frame
	Return:	YES => store is internal
----------------------------------------------------------------------------- */

BOOL
IsInternalStore(RefArg inStore)
{
	return [MakeNSString(GetFrameSlot(inStore, SYMA(kind))) isEqualToString: @"Internal"];
}


/* -----------------------------------------------------------------------------
	Determine whether a file is a Newton package.
	We accept file extension .newtonpkg and .pkg but in the latter case check
	the file isn’t really a macOS installer package.
	Args:		inURL			file URL
	Return:	YES => file is Newton pkg
----------------------------------------------------------------------------- */

BOOL
IsNewtonPkg(NSURL * inURL)
{
	NSString * extn = inURL.pathExtension;
	if ([extn compare:@"newtonpkg"] == NSOrderedSame) {
		return YES;
	}
	if ([extn compare:@"pkg"] == NSOrderedSame) {
		FILE * fref = fopen(inURL.fileSystemRepresentation, "r");
		if (fref) {
			char signature[8];
			fseek(fref, 0, SEEK_SET);
			fread(signature, 1, 7, fref);
			signature[7] = 0;
			fclose(fref);
			if (strcmp(signature, "package") == 0) {
				return YES;
			}
		}
	}
	return NO;
}


/* -----------------------------------------------------------------------------
	Extract the unique name from a package.
	Args:		inEntry			a package soup entry
	Return:	an auto-released NSString*
----------------------------------------------------------------------------- */
// package name is limited to 26 chars (NPG 2-11) -- allow a little more
#define kMaxPkgNameLength 63

NSString *
PackageName(RefArg inEntry)
{
	NSString * pkgName = nil;
	RefVar pkgRef(GetFrameSlot(inEntry, MakeSymbol("pkgRef")));
	if (IsBinary(pkgRef))
	{
		WITH_LOCKED_BINARY(pkgRef, pkgPtr)
		PackageDirectory * dir = (PackageDirectory *)pkgPtr;
		if (strncmp(dir->signature, "package", 7) == 0)
		{
			// extract the unique package name
			UniChar pkgNameStr[kMaxPkgNameLength+1], * s;
			unsigned pkgNameOffset = offsetof(PackageDirectory, parts) + CANONICAL_LONG(dir->numParts)*sizeof(PartEntry) + CANONICAL_SHORT(dir->name.offset);
			unsigned pkgNameLen = CANONICAL_SHORT(dir->name.length);
			if (pkgNameLen > kMaxPkgNameLength*sizeof(UniChar)) pkgNameLen = kMaxPkgNameLength*sizeof(UniChar);	// length is in bytes
			pkgNameStr[kMaxPkgNameLength] = 0;
			memcpy(pkgNameStr, (char *)pkgPtr+pkgNameOffset, pkgNameLen);	// name in pkg binary is nul-terminated
#if defined(hasByteSwapping)
			for (s = pkgNameStr; *s != 0; s++)
				*s = BYTE_SWAP_SHORT(*s);
#endif
			pkgName = [NSString stringWithCharacters: pkgNameStr length: pkgNameLen/sizeof(UniChar)-1];
		}
		END_WITH_LOCKED_BINARY(pkgRef)
	}
	return pkgName;
}


/*------------------------------------------------------------------------------
	Return details for a package file.
	Args:		inURL			putative package file
				outName		package name extracted from package directory
				outSize		package size
	Return:	error code	if error, outName & outSize are invalid
------------------------------------------------------------------------------*/

NewtonErr
GetPackageDetails(NSURL * inURL, NSString ** outName, unsigned int * outSize)
{
	NewtonErr err = kOSErrBadPackage;
	const char * pkgPathStr = inURL.fileSystemRepresentation;
	FILE * pkgFile = fopen(pkgPathStr, "r");
	if (pkgFile != NULL)
	{
		// verify that it’s actually a Newton package
		PackageDirectory  dir;
		fread(&dir, 1, sizeof(PackageDirectory), pkgFile);
		if (strncmp(dir.signature, "package", 7) == 0)
		{
			// extract the unique package name
			UniChar pkgNameStr[kMaxPkgNameLength+1], * s;
			unsigned pkgNameOffset = offsetof(PackageDirectory, parts) + CANONICAL_LONG(dir.numParts)*sizeof(PartEntry) + CANONICAL_SHORT(dir.name.offset);
			unsigned pkgNameLen = CANONICAL_SHORT(dir.name.length);
			if (pkgNameLen > kMaxPkgNameLength*sizeof(UniChar)) pkgNameLen = kMaxPkgNameLength*sizeof(UniChar);	// length is in bytes
			pkgNameStr[kMaxPkgNameLength] = 0;
			fseek(pkgFile, pkgNameOffset, SEEK_SET);
			fread(pkgNameStr, 1, pkgNameLen, pkgFile);	// name in pkg file is nul-terminated
#if defined(hasByteSwapping)
			for (s = pkgNameStr; *s != 0; s++)
				*s = BYTE_SWAP_SHORT(*s);
#endif
			*outName = [NSString stringWithCharacters: pkgNameStr length: pkgNameLen/sizeof(UniChar)-1];

			// tell its length
			fseek(pkgFile, 0, SEEK_END);
			*outSize = ftell(pkgFile);
			fseek(pkgFile, 0, SEEK_SET);

			err = noErr;
		}
		fclose(pkgFile);
	}
	return err;
}


#if 0
/* -----------------------------------------------------------------------------
	The screenshot function; a native function in the Toolkit app on Newton.
	Only here because, well, where else should it go?
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

Ref
ScreenShotFn(RefArg rcvr, RefArg ioCapture)
{
	PixelMap	screen;
	if (fn1B14(&screen))
	{
		//sp-24
		Rect	bounds;	//sp1Cr
		int	pixDepth = PixelDepth(&screen);	// r5
		BOOL	isSelection;	// r10
		if (isSelection = FrameHasSlotRef(ioCapture, MakeSymbol("top")))
		{
			bounds.top = RINT(GetFrameSlot(ioCapture, MakeSymbol("top")));
			bounds.bottom = RINT(GetFrameSlot(ioCapture, MakeSymbol("bottom")));
			bounds.left = RINT(GetFrameSlot(ioCapture, MakeSymbol("left")));
			bounds.right = RINT(GetFrameSlot(ioCapture, MakeSymbol("right")));

			if (bounds.top < screen.bounds.top)
				bounds.top = screen.bounds.top;
			if (bounds.bottom > screen.bounds.bottom)
				bounds.bottom = screen.bounds.bottom;
			if (bounds.left < screen.bounds.left)
				bounds.left = screen.bounds.left;
			if (bounds.right > screen.bounds.right)
				bounds.right = screen.bounds.right;

			bounds.left = ((bounds.left * pixDepth) & ~0x07) / pixDepth;	// align on byte
		}
		else
			bounds = screen.bounds;

		int	height = bounds.bottom - bounds.top;	// r8
		int	screenRowBytes = screen.rowBytes;				// r7
		int	rowBytes = ((bounds.right - bounds.left) * pixDepth + 7) / 8;	// r9
		rowBytes = LONGALIGN(rowBytes);	// r6
		SetFrameSlot(ioCapture, MakeSymbol("rowBytes"), MAKEINT(rowBytes));
		SetFrameSlot(ioCapture, MakeSymbol("top"), MAKEINT(bounds.top));
		SetFrameSlot(ioCapture, MakeSymbol("left"), MAKEINT(bounds.left));
		SetFrameSlot(ioCapture, MakeSymbol("bottom"), MAKEINT(bounds.bottom));
		SetFrameSlot(ioCapture, MakeSymbol("right"), MAKEINT(bounds.right));
		SetFrameSlot(ioCapture, MakeSymbol("depth"), MAKEINT(pixDepth));

		int		binSize = height * rowBytes;	// r8
		RefVar	theBits = AllocateBinary(NILREF, binSize);
		char *	theData = BinaryData(theBits);
		if (isSelection)
		{
		//	copy the selected box
			int  y;
			for (y = bounds.top; y < bounds.bottom; y++)
			{
				int	rowOffset = (bounds.left * pixDepth) / 8;
				memcpy(theData + (y - bounds.top) * rowBytes, screen.baseAddr + y * screenRowBytes + rowOffset, rowBytes);
			}
		}
		else
		//	copy the entire screen data
			memcpy(theData, screen.baseAddr, binSize);

		SetFrameSlot(ioCapture, MakeSymbol("theBits"), theBits);
	}
	return NILREF;
}

#endif
