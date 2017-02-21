/*
	File:		SourceListViewController.m

	Abstract:	Implementation of NTXSourceListViewController class.

	Written by:		Newton Research, 2014.
*/

#import "SourceListViewController.h"
#import "NCWindowController.h"
#import "KeyboardViewController.h"
#import "ScreenshotViewController.h"
#import "PreferenceKeys.h"
#import "NCDocument.h"
#import "Utilities.h"


#pragma mark - Split views
/* -----------------------------------------------------------------------------
	N C S o u r c e S p l i t V i e w C o n t r o l l e r
	The sidebar half of the split view can be collapsed by a button
	in the window.
----------------------------------------------------------------------------- */
@implementation NCSourceSplitViewController

- (void)viewDidLoad
{
	[super viewDidLoad];

	NCWindowController * wc = self.view.window.windowController;
	wc.sourceSplitController = self;
}

- (void)toggleCollapsed
{
	sourceListItem.animator.collapsed = !sourceListItem.isCollapsed;
}

@end


#pragma mark - NCSourceListViewController
/* -----------------------------------------------------------------------------
	N C S o u r c e L i s t V i e w C o n t r o l l e r
	The source list controller represents the NCProjectItems contained
	in the project document.
	It acts as the data source and delegate for the NSOutlineView.
----------------------------------------------------------------------------- */
@implementation NCSourceListViewController

- (void)viewDidLoad
{
	[super viewDidLoad];

	// set up sidebar items ready to be populated when Newton connects
	sourceList = [[NSMutableArray alloc] init];
	libraryNode  = nil;
	libraryStoreNodeToDelete = nil;

	// defer population until window has fully loaded
	dispatch_async(dispatch_get_main_queue(), ^{
		NCWindowController * wc = self.view.window.windowController;
		wc.sourceListController = self;

		[self populateSourceList];

		// start listening for notifications re: selection by dock protocol
		NCDocument * document = self.view.window.windowController.document;
		[NSNotificationCenter.defaultCenter addObserver:self
															selector:@selector(dockDidRequestKeyboard:)
																 name:kDockDidRequestKeyboardNotification
															  object:document];
		[NSNotificationCenter.defaultCenter addObserver:self
															selector:@selector(dockDidCancel:)
																 name:kDockDidCancelNotification
															  object:document];
	});
}


- (void)populateSourceList {
	NCDocument * document = [self.view.window.windowController document];
	self.representedObject = document;
	if (document.deviceObj) {
		NSTreeNode * tNode, * internalNode = nil;
		// add to the DEVICE group
		tNode = [NSTreeNode treeNodeWithRepresentedObject:document.deviceObj];
		[sourceList addObject:tNode];
		
		if (!document.isReadOnly && !document.isNewton1) {
			// also Keyboard and Display items for Keyboard Passthrough and Screenshot functions
			tNode = [NSTreeNode treeNodeWithRepresentedObject:[[NCKeyboardInfo alloc] init]];
			[sourceList addObject:tNode];
			tNode = [NSTreeNode treeNodeWithRepresentedObject:[[NCScreenshotInfo alloc] init]];
			[sourceList addObject:tNode];
		}

		NSArray * stores = document.stores;
		for (NCStore * store in stores) {
			NSTreeNode * tNode = [self makeStoreNode:store];
			// add it to the DEVICE group
			[sourceList addObject:tNode];

			// expand the internal store
			if (internalNode == nil && (store.isInternal || [document.deviceObj.is1xData boolValue])) {
				internalNode = tNode;
			}
		}

		if (document.libraryStores) {
			// add them to a new STORE LIBRARY group
			libraryNode  = [NSTreeNode treeNodeWithRepresentedObject:[[NCSourceGroup alloc] initGroup:@"STORE LIBRARY"]];
			[sourceList addObject:libraryNode];

			NSMutableArray * sidebarItems = libraryNode.mutableChildNodes;
			for (NCStore * store in document.libraryStores)
			{
				NSTreeNode * tNode = [self makeStoreNode:store];
				// add it to the LIBRARY group
				[sidebarItems addObject:tNode];
			}
			// expand LIBRARY
			[sidebarView expandItem:libraryNode];
		}

		// expand it
		[sidebarView reloadData];
		if (internalNode) {
			[sidebarView expandItem:internalNode];
		}
		[sidebarView selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
		[sidebarView setEnabled:YES];
	}
}


- (IBAction)doubleClickedItem:(NSOutlineView *)sender {
	id item = [sender itemAtRow:sender.clickedRow];
 	if ([self outlineView:sender shouldShowOutlineCellForItem:item]) {
		if ([sender isItemExpanded:item]) {
			[sender collapseItem:item];
		} else {
			[sender expandItem:item];
		}
	}
}


/* -----------------------------------------------------------------------------
	The dock did something affecting our selection.
----------------------------------------------------------------------------- */

- (void)dockDidRequestKeyboard:(NSNotification *)inNotification {
	//keyboard passthrough requested from Newton end
	[sidebarView selectRowIndexes:[NSIndexSet indexSetWithIndex:1] byExtendingSelection:NO];
}


- (void)dockDidCancel:(NSNotification *)inNotification {
	// show device view when other functions cancelled
	[sidebarView selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
}


- (void)disconnected {
	// remove keyboard and display views when disconnected
	if (sourceList.count > 2) {
		NSTreeNode * theNode = sourceList[1];
		id item = theNode.representedObject;
		if ([item isKindOfClass: [NCKeyboardInfo class]]) {
			[sourceList removeObjectsInRange:NSMakeRange(1,2)];
			[sidebarView selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
			[sidebarView reloadData];
		}
	}
}


/* -----------------------------------------------------------------------------
	Make a store.
----------------------------------------------------------------------------- */

- (NSTreeNode *)makeStoreNode:(NCStore *)inStore
{
	// create node for this store
	NSTreeNode * tNode = [NSTreeNode treeNodeWithRepresentedObject:inStore];
	// add apps belonging to this store
	NSArray * apps = [inStore apps:YES];
	if (apps.count > 0) {
		NSMutableArray * sidebarItems = tNode.mutableChildNodes;
		for (NCApp * app in apps) {
			NSTreeNode * aNode = [NSTreeNode treeNodeWithRepresentedObject:app];
			[sidebarItems addObject:aNode];
			// and their soups
			NSArray * soups = [inStore soupsInApp:app];
			NSMutableArray * appSoups = aNode.mutableChildNodes;
			for (NCSoup * soup in soups)
				[appSoups addObject: [NSTreeNode treeNodeWithRepresentedObject:soup]];
		}
	}
	return tNode;
}


- (void)refreshStore:(NCStore *)inStore app:(NCApp *)inApp
{
	// update the given app - a soup has been added
	// or, of course, the app may have been added
	for (NSTreeNode * stoNode in sourceList) {
		id obj = stoNode.representedObject;
		if ([obj isKindOfClass:NCStore.class] && [obj isEqualTo:inStore]) {
			// this is the store we are talking about
			NSMutableArray * appSoups = nil;
			NSMutableArray * appNodes = stoNode.mutableChildNodes;
			for (NSTreeNode * aNode in appNodes) {
				NCApp * app = aNode.representedObject;
				if ([app isEqualTo:inApp]) {
					// and this is the app
					appSoups = aNode.mutableChildNodes;
					[appSoups removeAllObjects];
				}
			}
			if (appSoups == nil) {
				// app does not yet exist in this store tree, so add it
				NSTreeNode * aNode = [NSTreeNode treeNodeWithRepresentedObject:inApp];
				[appNodes addObject:aNode];
				appSoups = aNode.mutableChildNodes;
			}

			NSArray * soups = [inStore soupsInApp:inApp];
			for (NCSoup * soup in soups) {
				[appSoups addObject:[NSTreeNode treeNodeWithRepresentedObject:soup]];
			}

			break;
		}
	}

	dispatch_async(dispatch_get_main_queue(), ^{
		[sidebarView reloadData];
	});
}


#pragma mark - NSOutlineView item deletion
/* -----------------------------------------------------------------------------
	Enable main menu item for above.
	Also for the Edit menu Delete item, which the current view controller might
	use.
	Args:		inItem
	Return:	YES => enable
----------------------------------------------------------------------------- */

- (BOOL)validateUserInterfaceItem:(id<NSValidatedUserInterfaceItem>)inItem
{
// Edit menu
	if (inItem.action == @selector(selectAll:))
		return NO;
	if (inItem.action == @selector(delete:)) {
		libraryStoreNodeToDelete = nil;
		// can delete old stores
		NSInteger rowIndex;
		if ((rowIndex = sidebarView.selectedRow) >= 0
		&&  [[sidebarView itemAtRow:rowIndex] parentNode] == libraryNode) {
			libraryStoreNodeToDelete = [sidebarView itemAtRow:rowIndex];
			return YES;
		}
	}

	return NO;
}


- (IBAction)selectAll:(id)sender
{ /* don’t do this */ }


- (IBAction)delete:(id)sender
{
	// if we are deleting a library store we probably need to maintain that state
	if (libraryStoreNodeToDelete) {
		NCStore * storeObj = libraryStoreNodeToDelete.representedObject;
		NSAlert * alert = [[NSAlert alloc] init];
		[alert addButtonWithTitle: NSLocalizedString(@"delete", nil)];
		[alert addButtonWithTitle: NSLocalizedString(@"cancel", nil)];
		[alert setMessageText: [NSString stringWithFormat:@"Deleting the %@ store from your library will permanently remove all the information and packages it contains from Newton Connection.", storeObj.name]];
		[alert setInformativeText:@"Your Newton device will not be affected."];
		[alert setShowsSuppressionButton:YES];
		[alert setAlertStyle: NSAlertStyleWarning];

		NSInteger result = [alert runModal];
		if (result == NSAlertFirstButtonReturn) {
			NCDocument * document = self.representedObject;
			// perform deletion from core data via document
			[document.deviceObj removeStoresObject: storeObj];
			// and remove node from the source view
			NSMutableArray * sidebarItems = [libraryNode mutableChildNodes];
			[sidebarItems removeObject:libraryStoreNodeToDelete];
			if (sidebarItems.count == 0) {
				[sourceList removeObject:libraryNode];
				libraryNode = nil;
			}
			[sidebarView reloadData];
		}
	}
}


#pragma mark - NSOutlineViewDelegate protocol
/* -----------------------------------------------------------------------------
	Determine whether an item is a group title.
----------------------------------------------------------------------------- */

- (BOOL)outlineView:(NSOutlineView *)inView isGroupItem:(id)inItem
{
	id node = [inItem representedObject];
	return [node isKindOfClass: [NCSourceGroup class]];
}

/* -----------------------------------------------------------------------------
	Show the disclosure triangle for stores
	and apps which contain more than one soup.
----------------------------------------------------------------------------- */

- (BOOL)outlineView:(NSOutlineView *)inView shouldShowOutlineCellForItem:(id)inItem;
{
	id node = [inItem representedObject];
//	return ![node isKindOfClass: [NCSourceGroup class]] && ![inItem isLeaf];
	return [node isKindOfClass: [NCStore class]]
		 || ([node isKindOfClass: [NCApp class]] && [inItem childNodes].count > 1);
}

//- (BOOL)outlineView:(NSOutlineView *)outlineView shouldExpandItem:(id)item;

/* -----------------------------------------------------------------------------
	We can NOT select:
		group name items
		apps that contain more than one soup
		keyboard or display during sync
----------------------------------------------------------------------------- */

- (BOOL)outlineView:(NSOutlineView *)inView shouldSelectItem:(id)inItem
{
	id node = [inItem representedObject];
	if ([node isKindOfClass: [NCSourceGroup class]])
		return NO;
	if ([node isKindOfClass: [NCApp class]] && [inItem childNodes].count > 1)
		return NO;
	NCDocument * document = self.representedObject;
	if (document.dock.operationInProgress > kScreenshotActivity
	&& ([node isKindOfClass: [NCKeyboardInfo class]]
	 || [node isKindOfClass: [NCScreenshotInfo class]]))
		return NO;
	return YES;
}


#pragma mark NSOutlineViewDataSource protocol

- (NSArray *)childrenForItem:(id)inItem
{
	return inItem ? [inItem childNodes] : sourceList;
}


/* -----------------------------------------------------------------------------
	Return the number of children a particular item has.
	Because we are using a standard tree of NSDictionary, we can just return
	the count.
----------------------------------------------------------------------------- */

- (NSInteger)outlineView:(NSOutlineView *)inView numberOfChildrenOfItem:(id)inItem
{
	NSArray * children = [self childrenForItem: inItem];
	return children.count;
}


/* -----------------------------------------------------------------------------
	NSOutlineView will iterate over every child of every item, recursively asking
	for the entry at each index. Return the item at a given index.
----------------------------------------------------------------------------- */

- (id)outlineView:(NSOutlineView *)inView child:(int)index ofItem:(id)inItem
{
    NSArray * children = [self childrenForItem: inItem];
    // This will return an NSTreeNode with our model object as the representedObject
    return children[index];
}


/* -----------------------------------------------------------------------------
	Determine whether an item can be expanded.
	In our case, if an item has children then it is expandable.    
----------------------------------------------------------------------------- */

- (BOOL)outlineView:(NSOutlineView *)inView isItemExpandable:(id)inItem
{
	return ![inItem isLeaf];
}


/* -----------------------------------------------------------------------------
	NSOutlineView calls this for each column in your NSOutlineView, for each item.
	Return what you want displayed in each column.
----------------------------------------------------------------------------- */

- (id)outlineView:(NSOutlineView *)inView viewForTableColumn:(NSTableColumn *)inColumn item:(id)inItem
{
	id<NCSourceItem> node = [inItem representedObject];

	NSTableCellView * cellView = [inView makeViewWithIdentifier:@"Source" owner:self];
	if (node.name) {
		cellView.textField.stringValue = node.name;
	}
	if (node.image) {
		cellView.imageView.image = node.image;
	}
	return cellView;
}


/* -----------------------------------------------------------------------------
	The selection changed -- update the placeholder view accordingly.
	Need to do this rather than outlineViewAction b/c we can also change
	the selection programmatically.
----------------------------------------------------------------------------- */

- (void)outlineViewSelectionDidChange:(NSNotification *)inNotification
{
	NSTreeNode * theNode = [sidebarView itemAtRow:sidebarView.selectedRow];
	id item = theNode.representedObject;
	if ([item isKindOfClass: [NCApp class]]) {
		item = theNode.childNodes[0].representedObject;
	}

	[self.view.window.windowController sourceSelectionDidChange:item];
}

@end


#pragma mark -
/* -----------------------------------------------------------------------------
	N C S o u r c e L i s t O u t l i n e V i e w
	Our source list does not allow Select All.
----------------------------------------------------------------------------- */

@implementation NCSourceListOutlineView

- (BOOL)validateMenuItem:(id<NSValidatedUserInterfaceItem>)item {
	return [((NCSourceListViewController *)self.delegate) validateUserInterfaceItem:item];
}


- (IBAction)selectAll:(id)sender {
	[((NCSourceListViewController *)self.delegate) selectAll:sender];
}


- (IBAction)delete:(id)sender {
	[((NCSourceListViewController *)self.delegate) delete:sender];
}

@end
