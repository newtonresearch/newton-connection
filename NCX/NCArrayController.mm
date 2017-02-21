/*
	File:		NCArrayController.h

	Abstract:	Interface for NCArrayController class.
					The NSTableView in the soup info pane is bound to an NSArrayController
					for its data. We need to subclass NSArrayController for drag and drop support.

					By default, items gets exported to single Newton Data.txt file
					But we know how to import/export:
						Dates		create single Newton Dates.ics file containing all selected entries
						Names		create single Newton Names.vcf file containing all selected entries
						Notes		create .txt .rtf .aif file per selected entry
						Works		create .txt .rtf .tiff .csv file per selected entry
						MAD Max	import ONLY: .mp3 file

	Snippets:

	SetArraySlot(slotsWeWant, 0, SYMA(class));
	SetArraySlot(slotsWeWant, 1, SYMA(title));
	SetArraySlot(slotsWeWant, 2, SYMA(labels));
	SetArraySlot(slotsWeWant, 3, SYMA(timestamp));
	SetArraySlot(slotsWeWant, 4, SYMA(_uniqueId));

	Date import relies on [session callExtension: 'meet' with: mtgFrame];
		and [session setCurrentSoup: MakeStringFromCString(kDateSoupNames[soupIndex])]


	Written by:	Newton Research, 2012.
*/

#import "NCArrayController.h"
#import "SoupViewController.h"
#import "AppDelegate.h"
#import "NCXPlugIn.h"


/* -----------------------------------------------------------------------------
	D a t a
	We don’t want to drop soup entries on the same view where we started to drag
	so keep track of that view.
----------------------------------------------------------------------------- */
static NSView * dragSourceView;


@implementation NCArrayController

#pragma mark Drag out -- export
/* -----------------------------------------------------------------------------
	Promise files for the dragged items.
  NCEntrys can be added to the pasteboard
  they provide kPasteboardTypeFileURLPromise (the promise)
			  and kPasteboardTypeFilePromiseContent (UTI of file)
	When an entry wants our data in one of the above representations, we'll get a call to the
	NSPasteboardItemDataProvider protocol method –pasteboard:item:provideDataForType:.
----------------------------------------------------------------------------- */

- (BOOL)tableView:(NSTableView *)inView writeRowsWithIndexes:(NSIndexSet *)inRowIndexes toPasteboard:(NSPasteboard *)inPasteboard {
	NSMutableArray * __block soupEntries = [NSMutableArray array];
//	NCXPlugInController * plugin = [NCXPlugInController sharedController];
//	[plugin prepareExport];

	// set file types exported for classes of selected items
	[inRowIndexes enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL * stop) {
		NCEntry * entry = [[self arrangedObjects] objectAtIndex:idx];
		NSPasteboardItem * pbItem = [NSPasteboardItem new];
// if this entry is combined with others into a single file, and we haven’t already seen this type, then make an HFS promise
// otherwise don’t make promises we can’t keep
//		if ([plugin aggregatesEntries:infoController.soup.app]) don’t add promise after the first
		[pbItem setDataProvider:entry forTypes:@[kDataTypeSoupEntry,(__bridge NSString *)kPasteboardTypeFilePromiseContent,(__bridge NSString *)kPasteboardTypeFileURLPromise]];
		[soupEntries addObject:pbItem];
	}];

	dragSourceView = inView;
	[inPasteboard clearContents];
	return [inPasteboard writeObjects:soupEntries];
}


/* -----------------------------------------------------------------------------
	Perform the actual export.
	Return the names of the files that were promised.
----------------------------------------------------------------------------- */

- (NSArray *)tableView:(NSTableView *)inView namesOfPromisedFilesDroppedAtDestination:(NSURL *)inDropDestination forDraggedRowsWithIndexes:(NSIndexSet *)inRowIndexes {
	NSMutableArray * __block filenames = [NSMutableArray array];
	NCXPlugInController * plugin = [NCXPlugInController sharedController];

	[plugin beginExport:infoController.soup.app context:infoController.document destination:inDropDestination];
	[inRowIndexes enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL * stop) {
		NCEntry * entry = [self.arrangedObjects objectAtIndex:idx];
		NSString * finame = [plugin export:entry];
		if (finame) {	// Names, for example, does not generate a file per entry
			[filenames addObject:finame];
		}
	}];
	NSString * finlname = [plugin endExport];
	if (finlname) {	// Names, for example, generates a file per soup
		[filenames addObject:finlname];
	}

	return filenames;
}


#pragma mark Drop in -- import
/* -----------------------------------------------------------------------------
	Validate files dropped onto table for import.
	Things that might be dropped on a soup entry table:
		.ics -> Dates com.apple.ical.ics
		.txt, .rtf, .rtfd -> Notes, Works  kUTTypePlainText, kUTTypeRTF
		.tiff -> Notes, Works kUTTypeImage, kUTTypeTIFF
		.csv -> Works public.comma-separated-values-text
		.vcf -> Names public.vcard (kUTTypeVCard)
		.mp3 -> MAD Max  public.mp3 (kUTTypeMP3)
		.pkg -> install package; invalidate sync

	importFormats = dictionary, key = app name
										 value = dict/array: UTI of acceptable file/data, class of importer

----------------------------------------------------------------------------- */

- (NSDragOperation)tableView:(NSTableView *)inView validateDrop:(id <NSDraggingInfo>)info proposedRow:(int)inRow proposedDropOperation:(NSTableViewDropOperation)inOp {
	// first see if there are any soup entries on the pasteboard -- we can always accept those
	BOOL __block hasSoup = NO;
	if (dragSourceView != inView) {	// source view must be different to this (destination) view
		[info enumerateDraggingItemsWithOptions:NSDraggingItemEnumerationConcurrent
			forView:inView
			classes:@[[NSPasteboardItem class]]
			searchOptions:nil 
			usingBlock:^(NSDraggingItem * draggingItem, NSInteger idx, BOOL * stop) {
				if ([[draggingItem.item types] containsObject:kDataTypeSoupEntry]) {
					hasSoup = YES;
					*stop = YES;
				}
			}];
	}
	if (hasSoup)
		return NSDragOperationCopy;

	// now see if there are any file URLs
	// key off infoController.soup.app.name for list of acceptable file types
	NSArray * acceptableTypes = [[NCXPlugInController sharedController] importTypesForApp:infoController.soup.app];
	// eg Works <- .txt, .rtf, .rtfd, .tiff, .vcs  but as UTIs
	if (acceptableTypes) {
//		[inView setDropRow: inRow dropOperation: NSTableViewDropAbove];
		if ([info.draggingPasteboard canReadObjectForClasses:@[[NSURL class]] options:@{NSPasteboardURLReadingFileURLsOnlyKey: [NSNumber numberWithBool:YES],
																												  NSPasteboardURLReadingContentsConformToTypesKey: acceptableTypes }])
			return NSDragOperationCopy;
	}
	return NSDragOperationNone;
}


/* -----------------------------------------------------------------------------
	Accept (import) dropped data.
----------------------------------------------------------------------------- */

- (BOOL)tableView:(NSTableView *)inView acceptDrop:(id <NSDraggingInfo>)info row:(int)inRow dropOperation:(NSTableViewDropOperation)inOp {
	NSPasteboard * pasteboard = [info draggingPasteboard];
	NSMutableArray * __block entriesToImport = [NSMutableArray array];

	// see if we have any soup entries dragged from another NCX window
	[info enumerateDraggingItemsWithOptions:NSDraggingItemEnumerationConcurrent
		forView:inView
		classes:@[[NSPasteboardItem class]]
		searchOptions:nil 
		usingBlock:^(NSDraggingItem * draggingItem, NSInteger idx, BOOL * stop) {
			NSPasteboardItem * pbItem = draggingItem.item;
			if ([pbItem.types containsObject:kDataTypeSoupEntry]) {
				// it’s a soup entry
				NSData * refData = [pbItem dataForType:kDataTypeSoupEntry];
				CPtrPipe pipe;
				pipe.init((void *)refData.bytes, refData.length, NO, nil);
				RefArg entryRef(UnflattenRef(pipe));
				// we can’t import packages as simply as that
				if (!EQRef(GetFrameSlot(entryRef, SYMA(class)), MakeSymbol("*package*"))) {
					// DON’T use the entry’s original id
					SetFrameSlot(entryRef, SYMA(_uniqueId), RA(NILREF));
					NCEntry * realEntry = [infoController.soup addEntry: entryRef];
					[entriesToImport addObject:realEntry];
				}
			}
		}];
	if (entriesToImport.count > 0) {
		[gNCNub importEntries:entriesToImport];
		return YES;
	}

	NSArray * acceptableTypes = [[NCXPlugInController sharedController] importTypesForApp:infoController.soup.app];
	if (acceptableTypes) {
		// see if we have any file URLs on the pasteboard that we can import
  		NSArray * urls = [pasteboard readObjectsForClasses:@[[NSURL class]] options:@{NSPasteboardURLReadingFileURLsOnlyKey: [NSNumber numberWithBool:YES],
																												NSPasteboardURLReadingContentsConformToTypesKey: acceptableTypes }];
		if (urls && urls.count > 0) {
			// yes; import those files
			[infoController import: urls];
			return YES;
		}
	}
	return NO;
}


/* -----------------------------------------------------------------------------
	This method is necessary only if you want to delete items by dragging them to the trash.
	In order to support drags to the trash, you need to implement draggedImage:endedAt:operation:
	and handle the NSDragOperationDelete operation.
	For any other operation, pass the message to the superclass.
----------------------------------------------------------------------------- */

- (void)draggedImage:(NSImage *)image endedAt:(NSPoint)screenPoint operation:(NSDragOperation)operation
{
	if (operation == NSDragOperationDelete) {
	  // Tell all of the dragged nodes to remove themselves from the model.
//		NSArray *selection = [(AppController *)[self dataSource] draggedNodes];
//		for (NSTreeNode *node in selection) {
//			[[[node parentNode] mutableChildNodes] removeObject:node];
//		}
	//	[self reloadData];
	//	[self deselectAll:nil];
	} else {
		[super draggedImage:image endedAt:screenPoint operation:operation];
	}
}

@end
