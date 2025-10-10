/*
	File:		WindowController.m

	Abstract:	Implementation of NCWindowController class.

	Written by:		Newton Research, 2008.

	TO DO:		sort soup columns based on underlying data, not string representation
					don’t allow Select All of outline view
*/

#import "NCWindowController.h"
#import "SoupViewController.h"
#import "NRProgressBox.h"
#import "NCDocument.h"
#import "DockEvent.h"
#import "NCXErrors.h"
#import "Logging.h"


/* -----------------------------------------------------------------------------
	N C W i n d o w C o n t r o l l e r
	The window controller displays device, store or soup info depending on the
	selection in the NSOutlineView. It acts as the data source and delegate for
	the NSOutlineView.
	Each node in the source list represents an NCSourceItem.
	When selected, an NCSourceItem instantiates its NCInfoController to show
	the detail view.
----------------------------------------------------------------------------- */
@interface NCWindowController ()
@property IBOutlet NRProgressBox * progressBox;
@end


		 void *StatusObserverContext = &StatusObserverContext;
static void *ProgressObserverContext = &ProgressObserverContext;
static void *subProgressObserverContext = &subProgressObserverContext;


@implementation NCWindowController

/* -----------------------------------------------------------------------------
	Don’t show .newtondevice file extension in window title.
	Ignore inDisplayName -- it’s just the device id.
	Use the device (owner’s) name.
----------------------------------------------------------------------------- */

- (NSString *)windowTitleForDocumentDisplayName:(NSString *)inDisplayName {
	NCDocument * theDocument = self.document;
	NSString * title;
	if (theDocument.deviceObj.is1xData)
		title = [inDisplayName stringByDeletingPathExtension];	// document loaded from .nbku
	else
		title = theDocument.deviceObj.visibleName;
	if (title == nil)
		title = @"Newton Connection";
	return title;
}


/* -----------------------------------------------------------------------------
	Initialize after nib has been loaded.
----------------------------------------------------------------------------- */

- (void)windowDidLoad {
	[super windowDidLoad];
	self.window.titleVisibility = NSWindowTitleHidden;
}


/* -----------------------------------------------------------------------------
	Un|collapse a split view.
----------------------------------------------------------------------------- */

- (IBAction)toggleCollapsed:(id)sender {
	if (((NSSegmentedControl *)sender).selectedSegment == 1) {
		[self.inspectorSplitController toggleCollapsed];
	} else {
		[self.sourceSplitController toggleCollapsed];
	}
}


#pragma mark Progress reporting
/* -----------------------------------------------------------------------------
	Observe changes to the dock’s status.
----------------------------------------------------------------------------- */

- (void)connected:(NCDockProtocolController *)inDock {
	if (inDock) {
		[inDock addObserver:self forKeyPath:@"statusText" options:NSKeyValueObservingOptionNew context:StatusObserverContext];
		[inDock setValue:@"Start by establishing a connection from your Newton device." forKeyPath:@"statusText"];
	} else {
		self.progressText = @"Archived data.";
	}
}


- (void)disconnected:(NCDockProtocolController *)inDock {
	if (inDock) {
		[inDock removeObserver:self forKeyPath:@"statusText"];
	}
	[self.sourceListController disconnected];
	[self stopProgress];
}


/* -----------------------------------------------------------------------------
	Observe changes to the document’s progress and update the progress box.
	Args:		sender
	Return:	--
----------------------------------------------------------------------------- */

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context {
	NCWindowController *__weak weakself = self;
	if (context == ProgressObserverContext) {
		NSProgress * progress = object;
		dispatch_async(dispatch_get_main_queue(), ^{
		//	if totalUnitCount < 0 it’s indeterminate
			weakself.progressBox.barValue = progress.fractionCompleted;
			weakself.progressBox.needsDisplay = YES;
	  });
	} else if (context == subProgressObserverContext) {
		if ([keyPath compare:NSStringFromSelector(@selector(localizedDescription))] == NSOrderedSame) {
			NSProgress * progress = object;
			dispatch_async(dispatch_get_main_queue(), ^{
MINIMUM_LOG { REPprintf("\nProgress: %s", progress.localizedDescription.UTF8String); }
				weakself.progressBox.statusText = progress.localizedDescription;
				weakself.progressBox.needsDisplay = YES;
		  });
		}
	} else if (context == StatusObserverContext) {
		dispatch_async(dispatch_get_main_queue(), ^{
			id statusText = [change objectForKey:@"new"];
			if (![statusText isKindOfClass:NSString.class]) {
				statusText = nil;
			}
			weakself.progressText = statusText;
		});

	} else {
		[super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
	}
}


- (void)startProgressFor:(id<NCComponentProtocol>)inHandler {
	if (self.overallProgress) {
		[self stopProgress];
	}
	self.overallProgress = [NSProgress progressWithTotalUnitCount:1];
	[self.overallProgress becomeCurrentWithPendingUnitCount:1];
	if (inHandler) {
		self.subProgress = [inHandler setupProgress];
	} else {
		self.subProgress = nil;
	}
	[self.overallProgress addObserver:self
						  forKeyPath:NSStringFromSelector(@selector(fractionCompleted))
							  options:NSKeyValueObservingOptionInitial
							  context:ProgressObserverContext];
	[self.subProgress addObserver:self
						  forKeyPath:NSStringFromSelector(@selector(localizedDescription))
							  options:NSKeyValueObservingOptionInitial
							  context:subProgressObserverContext];
	[self.overallProgress resignCurrent];

	self.overallProgress.cancellable = YES;
  dispatch_async(dispatch_get_main_queue(), ^(void){
    self.progressBox.canCancel = YES;
    self.progressBox.needsDisplay = YES;
  });
}


- (void)stopProgress {
	if (self.overallProgress) {
		[self.overallProgress removeObserver:self forKeyPath:NSStringFromSelector(@selector(fractionCompleted)) context:ProgressObserverContext];
		self.overallProgress = nil;
	}
	if (self.subProgress) {
		[self.subProgress removeObserver:self forKeyPath:NSStringFromSelector(@selector(localizedDescription)) context:subProgressObserverContext];
		self.subProgress = nil;
	}
	self.progressBox.statusText = nil;
	self.progressBox.barValue = -1.0;
  dispatch_async(dispatch_get_main_queue(), ^(void){
    self.progressBox.canCancel = NO;
    self.progressBox.needsDisplay = YES;
  });
}


- (void)setProgressText:(NSString *)progressText {
  dispatch_async(dispatch_get_main_queue(), ^(void){
    self.progressBox.statusText = progressText;
    if (progressText == nil) {
      self.progressBox.barValue = -1.0;
      self.progressBox.canCancel = NO;
    }
    self.progressBox.needsDisplay = YES;
  });
}

- (NSString *)progressText {
	return self.progressBox.statusText;
}


#pragma mark Cancellation
/*------------------------------------------------------------------------------
	Cancellation of an operation from the UI.
	The NCDockProtocolController knows how to cancel the operation in progress.
------------------------------------------------------------------------------*/

- (IBAction)cancelOperation:(id)sender {
	if (self.overallProgress && self.overallProgress.isCancellable)
		[self.overallProgress cancel];
	[((NCDocument *)self.document).dock cancelOperation];
}


#pragma mark Hardware Interface
/* -----------------------------------------------------------------------------
	Show hardware info.
----------------------------------------------------------------------------- */

- (void)populateSourceList {
	[self.sourceListController populateSourceList];
}

- (void)refreshStore:(NCStore *)inStore app:(NCApp *)inApp {
	[self.sourceListController refreshStore:inStore app:inApp];
}


#pragma mark Item View
/* -----------------------------------------------------------------------------
	Change the information subview.
----------------------------------------------------------------------------- */

- (void)sourceSelectionDidChange:(id<NCSourceItem>)item {
	if (item) {
		[self.contentController performSegueWithIdentifier:item.identifier sender:item];
	}
}


#pragma mark Sync
/* -----------------------------------------------------------------------------
	For Newton 1.
	Show the device info view progress gauge.
	This will automatically issue a [dock requestSync]
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (void) performSync {
	[self.deviceViewController startSync:self];
}

@end
