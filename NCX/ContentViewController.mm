/*
	File:		ContentViewController.mm

	Abstract:	Implementation of NCContentViewController subclasses.

	Written by:		Newton Research, 2015.
*/

#import "ContentViewController.h"
#import "NCWindowController.h"
#import "SoupViewController.h"


@interface EmptySegue : NSStoryboardSegue
@end

@implementation EmptySegue
- (void)perform {
    // Nothing. The NCContentViewController class handles all of the view controller action.
}
@end


/* -----------------------------------------------------------------------------
	N C C o n t e n t V i e w C o n t r o l l e r
----------------------------------------------------------------------------- */

@implementation NCContentViewController

- (void)viewDidLoad {
	[super viewDidLoad];

	dispatch_async(dispatch_get_main_queue(), ^{
		NCWindowController * wc = self.view.window.windowController;
		wc.contentController = self;
	});
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
		NCWindowController * wc = self.view.window.windowController;
		wc.inspector.info = [toViewController isKindOfClass: [NCSoupViewController class]] ? ((NCSoupViewController *)toViewController).entries : nil;

		NSView * subview = toViewController.view;
		// make sure our added subview is placed and resizes correctly
		if (subview) {
			((NCInfoController *)toViewController).document = wc.document;
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
