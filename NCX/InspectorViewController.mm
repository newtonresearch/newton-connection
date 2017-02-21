/*
	File:		InspectorViewController.mm

	Abstract:	Implementation of InspectorViewController subclasses.

	Written by:		Newton Research, 2015.
*/

#import "NCWindowController.h"
#import "SoupViewController.h"
#import "Newton/NewtonPackage.h"
#import "PkgPart.h"
#import "Utilities.h"
#import "NCXPlugIn.h"
#import <Quartz/Quartz.h>	// for QuickLookUI
//@import Quartz

extern void	RedirectStdioOutTranslator(FILE * inFRef);


/* -----------------------------------------------------------------------------
	N C I n s p e c t o r S p l i t V i e w C o n t r o l l e r
	We want to be able to (un)collapse the inspector view.
----------------------------------------------------------------------------- */
@implementation NCInspectorSplitViewController

- (void)viewDidLoad {
	[super viewDidLoad];

	NCWindowController * wc = self.view.window.windowController;
	wc.inspectorSplitController = self;
}

- (void)toggleCollapsed {
	inspectorItem.animator.collapsed = !inspectorItem.isCollapsed;
}

@end


/* -----------------------------------------------------------------------------
	I n s p e c t o r V i e w C o n t r o l l e r
----------------------------------------------------------------------------- */
@interface NCInspectorViewController ()
{
	NSArrayController * _ac;
	NSDictionary * txAttrs;
}
@end


@implementation NCInspectorViewController

- (void)viewDidLoad {
	[super viewDidLoad];

	// set up text attributes for inspector view
	NSFont * txFont = [NSFont fontWithName:@"Menlo" size:11.0];
	// calculate tab width
	NSFont * charWidthFont = [txFont screenFontWithRenderingMode:NSFontDefaultRenderingMode];
	NSInteger tabWidth = 3;	// [NSUserDefaults.standardUserDefaults integerForKey:@"TabWidth"];
	CGFloat charWidth = [@" " sizeWithAttributes:@{NSFontAttributeName:charWidthFont}].width;
	if (charWidth == 0)
		charWidth = charWidthFont.maximumAdvancement.width;
	// use a default paragraph style, but with the tab width adjusted
	NSMutableParagraphStyle * txStyle = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
	[txStyle setTabStops:[NSArray array]];
	[txStyle setDefaultTabInterval:(charWidth * tabWidth)];

	txAttrs = @{ NSFontAttributeName: txFont, NSParagraphStyleAttributeName: txStyle };

	dispatch_async(dispatch_get_main_queue(), ^{
		NCWindowController * wc = self.view.window.windowController;
		wc.inspector = self;
		NSFileManager * fileManager = NSFileManager.defaultManager;
		self.qlFolder = [ApplicationSupportFolder() URLByAppendingPathComponent:@"QuickLook" isDirectory:YES];
		if ([fileManager createDirectoryAtURL:self.qlFolder withIntermediateDirectories:YES attributes:nil error:nil]) {
			NSDirectoryEnumerator * iter = [fileManager enumeratorAtURL:self.qlFolder includingPropertiesForKeys:nil options:0 errorHandler:nil];
			for (NSURL * fileURL in iter) {
				[fileManager removeItemAtURL:fileURL error:nil];
			}
		}
	});
}


- (void)viewWillAppear {
	[super viewWillAppear];
	NCWindowController * wc = self.view.window.windowController;
	NSViewController * contentViewController = (wc.contentController.childViewControllers.count > 0)? wc.contentController.childViewControllers[0] : nil;
	self.info = [contentViewController isKindOfClass:[NCSoupViewController class]] ? ((NCSoupViewController *)contentViewController).entries : nil;
}

- (void)viewWillDisappear {
	[super viewWillDisappear];
	[self releaseInfo];
}


- (void)setInfo:(NSArrayController *)inEntries {
	[self releaseInfo];
	if (inEntries) {
		_ac = inEntries;
		[_ac addObserver:self forKeyPath:@"selectedObjects" options:NSKeyValueObservingOptionNew context:NULL];
		[self updatePanel:_ac.selectedObjects];
	} else {
		[self updatePanel:nil];
	}
}

- (NSArrayController *)info {
	return _ac;
}


- (void)releaseInfo {
	if (_ac) {
		[_ac removeObserver:self forKeyPath:@"selectedObjects"];
		_ac = nil;
	}
}


//	to receive change notifications:
- (void) observeValueForKeyPath:(NSString *)keyPath
							  ofObject:(id)object
								 change:(NSDictionary *)change
								context:(void *)context {
	if ([keyPath isEqual:@"selectedObjects"]) {
		[self updatePanel:[_ac selectedObjects]];
	}
}


/* -----------------------------------------------------------------------------
	Update the inspector panel.
	NSMutableArray* pkgViewControllers
		PkgInfoController*
			PkgInfo*
		PkgPartController* []
			PkgPart*
----------------------------------------------------------------------------- */

- (void)updatePanel:(NSArray *)inEntries {

	NSString * selectionInfo = nil;
	if (inEntries == nil) {
		selectionInfo = @"Inspector not applicable";
	} else if (inEntries.count == 0) {
		selectionInfo = @"No selection";
	} else if (inEntries.count > 1) {
		selectionInfo = @"Multiple selection";
	}
	if (selectionInfo) {
		self.text = [[NSAttributedString alloc] initWithString:selectionInfo];
		[self performSegueWithIdentifier:@"Status" sender:self];
		return;
	}

	//	there’s exactly one element in the array, display it
	NCEntry * entry = (NCEntry *)inEntries[0];

	if ([entry.refClass isEqualToString:kPackageRefClass]) {
		CPtrPipe pipe;
		pipe.init((void *)entry.refData.bytes, entry.refData.length, NO, NULL);
		RefVar refEntry(UnflattenRef(pipe));
		RefVar pkgRef(GetFrameSlot(refEntry, MakeSymbol("pkgRef")));
		if (IsBinary(pkgRef)) {
			CDataPtr pkgPtr(pkgRef);
			NewtonPackage pkg((void *)(char *)pkgPtr);
			const PackageDirectory * dir = pkg.directory();
			PkgInfo * pkgInfo = [[PkgInfo alloc] initWithDirectory:dir];

			ArrayIndex partCount = dir->numParts;
			for (ArrayIndex partNum = 0; partNum < partCount; ++partNum) {
				const PartEntry * thePart = pkg.partEntry(partNum);

				PkgPart * partObj;
				unsigned int partType = thePart->type;
				if (partType == 'form'
				||  partType == 'auto')
					partObj = [PkgFormPart alloc];
				else if (partType == 'book')
					partObj = [PkgBookPart alloc];
				else
					partObj = [PkgPart alloc];
				partObj = [partObj init:thePart ref:pkg.partRef(partNum) data:pkg.partPkgData(partNum)->data sequence:partNum];
				[pkgInfo addPart:partObj];
			}
			[self performSegueWithIdentifier:@"Package" sender:pkgInfo];
		}

	} else {
		NSString * filename, * finalFilename;
		NCXPlugInController * pluginController = NCXPlugInController.sharedController;
		[pluginController beginExport:NULL context:self.view.window.windowController.document destination:self.qlFolder];
		filename = [pluginController export:entry];
		finalFilename = [pluginController endExport];
		if (filename == NULL) {
			filename = finalFilename;
		}
		if (filename == NULL) {
			filename = @"Untitled";
		}
		self.url = [self.qlFolder URLByAppendingPathComponent:filename];
		[self performSegueWithIdentifier:@"QuickLook" sender:self];
	}
}


- (void)prepareForSegue:(NSStoryboardSegue *)segue sender:(id)sender {
	NSViewController * toViewController = (NSViewController *)segue.destinationController;
	toViewController.representedObject = sender;
	NSViewController * fromViewController = (self.childViewControllers.count > 0)? self.childViewControllers[0] : nil;

	[self addChildViewController:toViewController];
//	[self transitionFromViewController:fromViewController toViewController:toViewController options:0 completionHandler:^{[fromViewController removeFromParentViewController];}];
	[self transitionFromViewController:fromViewController toViewController:toViewController];
}


- (void)transitionFromViewController:(NSViewController *)fromViewController toViewController:(NSViewController *)toViewController {
	// remove any previous item view
	if (fromViewController) {
		[fromViewController.view removeFromSuperview];
		[fromViewController removeFromParentViewController];
	}

	if (toViewController) {
		NSView * subview = toViewController.view;
		// make sure our added subview is placed and resizes correctly
		if (subview) {
			[subview setTranslatesAutoresizingMaskIntoConstraints:NO];
			[self.view addSubview:subview];
			[self.view addConstraint:[NSLayoutConstraint constraintWithItem:subview attribute:NSLayoutAttributeLeft	 relatedBy:NSLayoutRelationEqual toItem:self.view attribute:NSLayoutAttributeLeft	multiplier:1 constant:0]];
			[self.view addConstraint:[NSLayoutConstraint constraintWithItem:subview attribute:NSLayoutAttributeRight	 relatedBy:NSLayoutRelationEqual toItem:self.view attribute:NSLayoutAttributeRight	multiplier:1 constant:0]];
			[self.view addConstraint:[NSLayoutConstraint constraintWithItem:subview attribute:NSLayoutAttributeTop	 relatedBy:NSLayoutRelationEqual toItem:self.view attribute:NSLayoutAttributeTop		multiplier:1 constant:0]];
			[self.view addConstraint:[NSLayoutConstraint constraintWithItem:subview attribute:NSLayoutAttributeBottom relatedBy:NSLayoutRelationEqual toItem:self.view attribute:NSLayoutAttributeBottom	multiplier:1 constant:0]];
		}
	}
}

@end


/* -----------------------------------------------------------------------------
	N C S t a t u s V i e w C o n t r o l l e r
----------------------------------------------------------------------------- */

@implementation NCStatusViewController
@end


/* -----------------------------------------------------------------------------
	Q u i c k L o o k V i e w C o n t r o l l e r
----------------------------------------------------------------------------- */
@interface QuickLookViewController ()
{
	QLPreviewView * previewView;
}
@end


@implementation QuickLookViewController

- (void)viewDidLoad {
	[super viewDidLoad];

	// create QuickLook view
	previewView = [[QLPreviewView alloc] initWithFrame:self.view.bounds style:QLPreviewViewStyleNormal];
	// constrain it to its container
	[previewView setTranslatesAutoresizingMaskIntoConstraints:NO];
	[self.view addSubview:previewView];
	[self.view addConstraint:[NSLayoutConstraint constraintWithItem:previewView attribute:NSLayoutAttributeLeft	  relatedBy:NSLayoutRelationEqual toItem:self.view attribute:NSLayoutAttributeLeft   multiplier:1 constant:0]];
	[self.view addConstraint:[NSLayoutConstraint constraintWithItem:previewView attribute:NSLayoutAttributeRight  relatedBy:NSLayoutRelationEqual toItem:self.view attribute:NSLayoutAttributeRight  multiplier:1 constant:0]];
	[self.view addConstraint:[NSLayoutConstraint constraintWithItem:previewView attribute:NSLayoutAttributeTop	  relatedBy:NSLayoutRelationEqual toItem:self.view attribute:NSLayoutAttributeTop	 multiplier:1 constant:0]];
	[self.view addConstraint:[NSLayoutConstraint constraintWithItem:previewView attribute:NSLayoutAttributeBottom relatedBy:NSLayoutRelationEqual toItem:self.view attribute:NSLayoutAttributeBottom multiplier:1 constant:0]];
	previewView.previewItem = [self.representedObject url];
}

@end


/* -----------------------------------------------------------------------------
	N C T e x t V i e w C o n t r o l l e r
----------------------------------------------------------------------------- */

@implementation NCTextViewController

- (void)viewDidLoad {
	[super viewDidLoad];

	textView.textContainerInset = NSMakeSize(4,4);
}

@end


/* -----------------------------------------------------------------------------
	N C P a c k a g e V i e w C o n t r o l l e r
----------------------------------------------------------------------------- */

@implementation NCPackageViewController

- (void)viewDidLoad {
	[super viewDidLoad];

	NSStoryboard * sb = self.storyboard;
	// add view controllers for package part info
	NSView * prevview = self.infoView;
	for (PkgPart * part in ((PkgInfo *)self.representedObject).parts) {
		NSViewController * viewController = [sb instantiateControllerWithIdentifier:[self viewControllerNameFor:part.partType]];
		NSView * subview = viewController.view;
		NSStackView * superview = self.stackView;
		viewController.representedObject = part;
		[subview setTranslatesAutoresizingMaskIntoConstraints:NO];
		[superview addSubview:subview];
		[superview addConstraint:[NSLayoutConstraint constraintWithItem:subview attribute:NSLayoutAttributeLeft	 relatedBy:NSLayoutRelationEqual toItem:superview attribute:NSLayoutAttributeLeft	multiplier:1 constant:0]];
		[superview addConstraint:[NSLayoutConstraint constraintWithItem:subview attribute:NSLayoutAttributeRight	 relatedBy:NSLayoutRelationEqual toItem:superview attribute:NSLayoutAttributeRight	multiplier:1 constant:0]];
		[superview addConstraint:[NSLayoutConstraint constraintWithItem:subview attribute:NSLayoutAttributeTop	 relatedBy:NSLayoutRelationEqual toItem:prevview attribute:NSLayoutAttributeBottom	multiplier:1 constant:0]];
//		[superview addConstraint:[NSLayoutConstraint constraintWithItem:subview attribute:NSLayoutAttributeBottom relatedBy:NSLayoutRelationEqual toItem:superview attribute:NSLayoutAttributeBottom	multiplier:1 constant:0]];
		prevview = subview;
	}
}


/* -----------------------------------------------------------------------------
	Return storyboard viewcontroller id for part type.
----------------------------------------------------------------------------- */

- (NSString *)viewControllerNameFor:(unsigned int)inType {
	NSString * name;
	switch (inType) {
	case 'form':
	case 'auto':
		name = @"formPartViewController";
		break;
	case 'book':
		name = @"bookPartViewController";
		break;
//	case 'soup':
//		name = @"soupPartViewController";
//		break;
	default:
		name = @"PartViewController";
		break;
	}
	return name;
}


@end

