/*
	File:		InspectorViewController.h

	Abstract:	The InspectorViewController prints selected soup entries.

	Written by:		Newton Research, 2015.
*/

#import <AppKit/AppKit.h>
#import "NewtonKit.h"


/* -----------------------------------------------------------------------------
	N C I n s p e c t o r S p l i t V i e w C o n t r o l l e r
	The divider in the content split can be collapsed.
----------------------------------------------------------------------------- */
@interface NCInspectorSplitViewController : NSSplitViewController
{
	IBOutlet NSSplitViewItem * inspectorItem;
}
- (void)toggleCollapsed;
@end


/* -----------------------------------------------------------------------------
	I n s p e c t o r V i e w C o n t r o l l e r
	Controller for the inspector view.
----------------------------------------------------------------------------- */

@interface NCInspectorViewController : NSViewController
@property(strong) NSArrayController * info;
@property(strong) NSAttributedString * text;
@property(strong) NSURL * qlFolder;
@property(strong) NSURL * url;
@end


@interface NCStatusViewController : NSViewController
@end


@interface QuickLookViewController : NSViewController
@end


@interface NCTextViewController : NSViewController
{
	IBOutlet NSTextView * textView;
}
@end


@interface NCPackageViewController : NSViewController
@property(readonly) NSString * name;
@property(readonly) NSString * size;
@property(readonly) NSString * ident;
@property(readonly) NSString * version;
@property(readonly) NSString * copyright;
@property(readonly) NSString * creationDate;
@property(readonly) BOOL isCopyProtected;
@property(readonly) NSMutableArray/*<PkgPart>*/ * parts;

@property IBOutlet NSStackView * stackView;
@property IBOutlet NSView * infoView;
@end
