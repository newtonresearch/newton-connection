/*
	File:		Utilities.h

	Contains:	Utility functions for the NCX app.

	Written by:	Newton Research Group, 2005.
*/

#import <Foundation/Foundation.h>
#import "PlugInUtilities.h"

extern BOOL			IsInternalStore(RefArg inStore);
extern BOOL			IsNewtonPkg(NSURL * inURL);
extern NSString *	PackageName(RefArg inEntry);
extern NewtonErr	GetPackageDetails(NSURL * inURL, NSString ** outName, unsigned int * outSize);
