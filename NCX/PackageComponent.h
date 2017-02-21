/*
	File:		PackageComponent.h

	Contains:	Declarations for the NCX package installation controller.

	Written by:	Newton Research, 2009.
*/

#import "NCDockProtocolController.h"


#define kDDoLoadPackageFiles		'LPFL'


@interface NCPackageComponent : NCComponent
{
	BOOL isPkgFinderExtensionInstalled;
	NSURL * pkgURL;
	NSString * pkgName;
	NewtonErr pkgResult;
}

- (void) do_LPFL: (NCDockEvent *) inEvent;
- (void) do_LNPF: (NCDockEvent *) inEvent;
- (void) do_LPKG: (NCDockEvent *) inEvent;
- (void) do_lpfl: (NCDockEvent *) inEvent;

- (void) installPackageExtensions;
- (void) setPackageURL: (NSURL *) inURL name: (NSString *) inName;
- (void) nextPackage;
- (void) loadPackage: (NSURL *) inURL;

@end
