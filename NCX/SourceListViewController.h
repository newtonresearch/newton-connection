/*
	File:		SourceListViewController.h

	Abstract:	The NCSourceListViewController controls the source list sidebar view.
					The source list is an NSOutlineView representing an NCDeviceObj.

	Written by:		Newton Research, 2015.
*/

#import <Cocoa/Cocoa.h>
#import "NCStore.h"


/* -----------------------------------------------------------------------------
	N C S o u r c e S p l i t V i e w C o n t r o l l e r
	We want to be able to (un)collapse the source list view.
----------------------------------------------------------------------------- */
@interface NCSourceSplitViewController : NSSplitViewController
{
	IBOutlet NSSplitViewItem * sourceListItem;
}
- (void)toggleCollapsed;
@end


/* -----------------------------------------------------------------------------
	N C S o u r c e L i s t V i e w C o n t r o l l e r
	A list of source items, typically soups.
----------------------------------------------------------------------------- */

@interface NCSourceListViewController : NSViewController
{
	// outline sidebar
	IBOutlet NSOutlineView * sidebarView;

	// outline sidebar
	NSMutableArray<NSTreeNode *> * sourceList;
	NSTreeNode * deviceNode;
	NSTreeNode * libraryNode;
	NSTreeNode * libraryStoreNodeToDelete;
}
- (void)populateSourceList;
- (void)refreshStore:(NCStore *)inStore app:(NCApp *)inApp;
- (void)disconnected;

@end


/* -----------------------------------------------------------------------------
	N C S o u r c e L i s t O u t l i n e V i e w
	Our source list does not allow Select All.
----------------------------------------------------------------------------- */

@interface NCSourceListOutlineView : NSOutlineView
@end
