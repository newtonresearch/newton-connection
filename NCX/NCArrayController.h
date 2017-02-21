/*
	File:		NCArrayController.h

	Abstract:	Interface for NCArrayController class.
					The NSTableView in the soup info pane is bound to an NSArrayController
					for its data. We need to subclass NSArrayController for drag and drop support.

	Written by:	Newton Research, 2012.
*/

#import <Cocoa/Cocoa.h>

@class NCSoupViewController;

@interface NCArrayController : NSArrayController
{
	IBOutlet NCSoupViewController * infoController;
}

- (BOOL) tableView: (NSTableView *) inView writeRowsWithIndexes: (NSIndexSet *) inRowIndexes toPasteboard: (NSPasteboard *) inPasteboard;
- (NSArray *) tableView: (NSTableView *) inView namesOfPromisedFilesDroppedAtDestination: (NSURL *) inDropDestination forDraggedRowsWithIndexes: (NSIndexSet *) indexSet;
- (NSDragOperation) tableView: (NSTableView *) inView validateDrop: (id <NSDraggingInfo>) info proposedRow: (int) inRow proposedDropOperation: (NSTableViewDropOperation) inOp;
- (BOOL) tableView: (NSTableView *) inView acceptDrop: (id <NSDraggingInfo>) info row: (int) inRow dropOperation: (NSTableViewDropOperation) inOp;

@end
