/*
	File:		PackageComponent.mm

	Contains:	The NCX package installation controller.

	Written by:	Newton Research, 2009.
*/

#import "PackageComponent.h"
#import "NCDocument.h"
#import "Utilities.h"
#import "NCXErrors.h"
#import "Newton/PackageParts.h"

#define kDDoLoadNextPackageFile	'LNPF'
#define kDDoOverwritePackage		'LPKG'


@implementation NCPackageComponent

/*------------------------------------------------------------------------------
	Initialize ivars for new connection session.
	Args:		inSession
	Return:	--
------------------------------------------------------------------------------*/

- (id)initWithProtocolController:(NCDockProtocolController *)inController {
	if (self = [super initWithProtocolController: inController]) {
		isPkgFinderExtensionInstalled = NO;
	}
	return self;
}


/*------------------------------------------------------------------------------
	If we haven’t loaded the package finder extension this session
	then load it now.
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/

- (void)installPackageExtensions {
	if (!isPkgFinderExtensionInstalled) {
		[self.dock.session loadExtension:@"pfnd"];
		isPkgFinderExtensionInstalled = YES;
	}
}


/*------------------------------------------------------------------------------
	Return the event command tags handled by this component.
	Args:		--
	Return:	array of strings
------------------------------------------------------------------------------*/

- (NSArray *)eventTags {
	return [NSArray arrayWithObjects:@"LPFL",	// load pkgs from desktop
												@"lpfl",	// kDLoadPackageFile - load pkgs browsed from Newton
												@"LPKG",	// kDDoOverwritePackage - already installed
												@"LNPF",	// kDDoLoadNextPackageFile - already installed
												nil ];
}


/*------------------------------------------------------------------------------
	N e w t o n   D o c k   I n t e r f a c e
------------------------------------------------------------------------------*/

#pragma mark Newton Event Handlers
/*------------------------------------------------------------------------------
	Load a package file.
	The file name has already been browsed from the Newton.
				kDLoadPackageFile
	Args:		inEvent		event data is a ref containing the path
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_lpfl:(NCDockEvent *)inEvent {
	pkgResult = noErr;

	//	Install browsed package
	RefVar filename(inEvent.ref);
	if (IsFrame(filename))
		filename = GetFrameSlot(filename, SYMA(name));

	unsigned int pkSize;
	NSString * pkName;
	NSURL * pkURL = [self.dock filePath:filename];
	if (GetPackageDetails(pkURL, &pkName, &pkSize) == noErr) {
		// show progress -- this will be closed on -installDone
		self.dock.totalPkgSize = pkSize;	// bleagh!
		[self.dock resetProgressFor:self];

		[self setPackageURL:pkURL name:pkName];
		[self loadPackage:pkURL];
	}

	[self.dock installDone:pkgResult];
}


#pragma mark Desktop Event Handlers
/*------------------------------------------------------------------------------
	Start loading package files.
				kDDoLoadPackageFiles
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (NSProgress *)setupProgress {
	self.progress = [NSProgress progressWithTotalUnitCount:self.dock.totalPkgSize];
	self.progress.localizedDescription = NSLocalizedString(@"preparing install", nil);
	return self.progress;
}

- (void)do_LPFL:(NCDockEvent *)inEvent {
	pkgResult = noErr;

	if (self.dock.protocolVersion >= kDanteProtocolVersion) {
		[self.dock setDesktopControl:YES];
		[self installPackageExtensions];
		[self.dock.session sendEvent:kDRequestToInstall];
		[self.dock.session receiveResult];
		// do something with the result
	}
	[self nextPackage];	// or in this case, first package
}


/*------------------------------------------------------------------------------
	Load next package file.
				kDDoLoadNextPackageFile
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_LNPF:(NCDockEvent *)inEvent {
	[self nextPackage];
}


/*------------------------------------------------------------------------------
	Overwrite package on Newton device.
				kDDoOverwritePackage
	Args:		inEvent
	Return:	--
------------------------------------------------------------------------------*/

- (void)do_LPKG:(NCDockEvent *)inEvent {
	NewtonErr err;

	// remove package from default store
	[self.dock.session setCurrentStore:RA(NILREF) info:NO];
	[self.dock.session sendEvent:kDRemovePackage ref:MakeString(pkgName)];
	err = [self.dock.session receiveResult];
	if (err == noErr) {
		[self loadPackage:pkgURL];
	} else {
		pkgResult = err;
		self.dock.statusText = [NSString stringWithFormat:@"Failed to remove package “%@” (error %d)", pkgName, err];
	}
	[self nextPackage];
}

#pragma mark -

/*------------------------------------------------------------------------------
	Set state: current package path and name.
	Args:		inURL
				inName
	Return:	--
------------------------------------------------------------------------------*/

- (void)setPackageURL:(NSURL *)inURL name:(NSString *)inName {
	pkgURL = inURL;
	pkgName = inName;
}				


/*------------------------------------------------------------------------------
	Install the next package in the queue.
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/

- (void)nextPackage {
	NSDictionary * pkDict;
	while ((pkDict = [self.dock dequeuePackage]) != nil && pkgResult == noErr) {
		if (self.progress.isCancelled) {
			pkgResult = kNCErrOperationCancelled;
		} else {
			[self setPackageURL: [pkDict objectForKey: @"URL"] name: [pkDict objectForKey: @"name"]];

			if (self.dock.protocolVersion >= kDanteProtocolVersion)
			{
				// determine whether package is already installed
				RefVar	nameFrame(AllocateFrame());
				SetFrameSlot(nameFrame, MakeSymbol("packageName"), MakeString(pkgName));

				NCDockEvent * evt = [self.dock.session callExtension:'pfnd' with:nameFrame];
				if (evt.tag == kDOperationCanceled) {
					[self.dock.session sendEvent:kDOpCanceledAck];
					pkgResult = kNCErrOperationCancelled;
				} else if (evt.tag != 'pkno') {
					// evt == 'pkya' or 'tran' or 'dres'
					// assume package is already installed -- ask whether we should overwrite it
					dispatch_async(dispatch_get_main_queue(), ^{
						NSAlert * alert = [[NSAlert alloc] init];
						[alert addButtonWithTitle: NSLocalizedString(@"overwrite", nil)];
						[alert addButtonWithTitle: NSLocalizedString(@"cancel", nil)];
						[alert setMessageText: [NSString stringWithFormat: NSLocalizedString(@"already installed", nil), pkgName]];
						[alert setAlertStyle: NSAlertStyleCritical];

						[alert beginSheetModalForWindow: [self.dock.document windowForSheet]
								 completionHandler:^(NSModalResponse returnCode) {
								 	if (returnCode == NSAlertFirstButtonReturn) {
										// package exists; user has said yes, overwrite it
										[self.dock.session doEvent:kDDoOverwritePackage];
									} else {
										// move along
										[self.dock.session doEvent:kDDoLoadNextPackageFile];
									}
								 }];
					});
					return;
				} else {
					// no, not already installed -- install it
					[self loadPackage:pkgURL];
				}
			} else {
				// simple install for Newton OS 1
				[self loadPackage:pkgURL];
			}
		}
	}

	if (self.dock.protocolVersion >= kDanteProtocolVersion) {
		if (pkgResult != kNCErrOperationCancelled || self.progress.isCancelled)
			[self.dock.session sendEvent:kDOperationDone];	// no reply expected
	} else {
		// can’t do anything else in Newton OS 1
		[self.dock.session sendEvent:kDDisconnect];		// no reply expected
	}
	[self.dock installDone: pkgResult];
}


/*------------------------------------------------------------------------------
	Install a Newton package.
	Args:		inURL			path to package file
	Return:	--
------------------------------------------------------------------------------*/

- (void)loadPackage:(NSURL *)inURL {
	unsigned int pkSize;
	NSString * pkName;
	if (GetPackageDetails(inURL, &pkName, &pkSize) == noErr) {
		unsigned int chunkSize = pkSize < 32768 ? 256 : 1024;
		self.progress.localizedDescription = [NSString stringWithFormat:@"Installing “%@”", pkName];

		newton_try
		{
			if (self.dock.protocolVersion >= kDanteProtocolVersion) {
				// install onto default store
				[self.dock.session setCurrentStore:RA(NILREF) info:NO];
			}

			NCProgressCallback cb = ^(unsigned int totalAmount, unsigned int amountDone) {
				self.progress.completedUnitCount = amountDone;
			};

			[self.dock.session sendPackage:inURL callback:cb frequency:chunkSize];
			NCDockEvent * evt = [self.dock.session receiveEvent:kDAnyEvent];
			if (evt.tag == kDResult) {
				pkgResult = evt.value;
			} else if (evt.tag == kDOperationCanceled) {
				[self.dock.session sendEvent:kDOpCanceledAck];
				pkgResult = kNCErrOperationCancelled;
			}
		}
		newton_catch_all
		{
//		pkgResult = (NewtonErr)(long)CurrentException()->data;
//		REPprintf("\n#### Exception %s (%d) during package installation.\n", CurrentException()->name, pkgResult);
		//	need to cancel further package installation?
		}
		end_try;
	}
}

@end
