/*
	File:		SoupViewController.mm

	Abstract:	Implementation of NCSoupViewController.

	Written by:		Newton Research, 2011.
*/

#import "SoupViewController.h"
#import "NCWindowController.h"
#import "BackupDocument.h"
#import "NCXPlugIn.h"
#import "PreferenceKeys.h"
#import "Utilities.h"

extern NSDateFormatter * gDateFormatter;


/* -----------------------------------------------------------------------------
	N C S o u p V i e w C o n t r o l l e r
	The Soup Info view contains an NSTableView of soup entries.
	Selected soup entries can be exported by dragging to the Finder.

	Using an NSArrayController to supply the entries from a persistent document:
	NSArrayController needs
		MOC				doc.objContext
		content set		NCSoup.entries

	Set up the table columns procedurally:
	column setup is an array of {tag, header, entry-property, entry-slot, transformer-function} per column
	these are predefined in a plist for known soups.
	All soups show _uniqueId and _modTime columns.
	Unknown soups just show a title column in addition to these.

	Calls
		name, phoneNumber, callDate, toRef, fromRef, notes
	Dates
		time, text
	DiplomatDNS
		targetDomainName, targetIPAddress
	IOBox
		fromName, fromRef, toRef, title, body.text, date
	Names
		person, company, phone, address
	Notes
		title, text, date
	Packages
		packageName, text (=>icon)
	System Information
		tag
	Time Zones
		name, country, gmt, areaCode
	diplomat
		setupName, userName, linkLayer
	Works

	Some of those slots are going to need more than string conversion.

----------------------------------------------------------------------------- */
extern NSDictionary * gSlotDict;
extern NSDictionary * gUserFolders;

extern NSString *	MakeNSString(RefArg inStr);


@implementation NCSoupViewController

@synthesize entries;
@synthesize soup;

- (void)viewWillAppear
{
	[super viewWillAppear];

//NSLog(@"-[NCSoupInfoController viewWillAppear] setting soup");
	[self setValue:self.representedObject forKey:@"soup"];

	gUserFolders = self.document.userFolders;

	// remove existing columns
//	[entries unbind:@"contentSet"];
//	[entries setContent:nil];
	NSArray * colms;
	while (colms = [_tableView tableColumns], [colms count] > 0)
		[_tableView removeTableColumn:[colms lastObject]];

	NSDictionary * isFiled = gSlotDict[@"*show-labels*"];
	NSMutableArray * allColms = [NSMutableArray arrayWithCapacity:[colms count] + 2];
	[allColms addObject:gSlotDict[@"*uniqueId*"]];
	if ((colms = gSlotDict[self.soup.name]) != nil)
		[allColms addObjectsFromArray:colms];
	else
		[allColms addObject:gSlotDict[@"*default*"]];
	if (isFiled[self.soup.name] != nil)
		[allColms addObject:gSlotDict[@"*labels*"]];
	[allColms addObject:gSlotDict[@"*modTime*"]];
	self.soup.columnInfo = allColms;

//	if info exists then load column widths/order/sorting
// elements are:
//		integer columnWidth
//		sorting - is ascending / is selected

//	NSString * colmKey = [NSString stringWithFormat:@"NSTableView Columns %@-soup", self.soup.name];
//	NSArray * colmDefs = [NSUserDefaults.standardUserDefaults arrayForKey: colmKey];

	// add columns for this soup
	for (NSDictionary * colDef in allColms)
	{
		NSString * tag = colDef[@"tag"];
		NSTableColumn * colm = [[NSTableColumn alloc] initWithIdentifier:tag];
		[colm.headerCell setStringValue:colDef[@"header"]];
		colm.editable = NO;
		colm.minWidth =   20.0;
		colm.maxWidth = 2000.0;
		colm.width = 100.0;		// _tableView.frame.size.width is 540 -- do we want to be clever about filling this width?
		// make text small
		[colm.dataCell setFont:[NSFont systemFontOfSize:[NSFont smallSystemFontSize]]];
		// format dates
		NSString * colType = colDef[@"type"];
		if ([colType isEqualToString:@"date"]
		||  [colType isEqualToString:@"dateRef"])
			[[colm dataCell] setFormatter:gDateFormatter];
		NSSortDescriptor * sorter = [NSSortDescriptor sortDescriptorWithKey:tag ascending:YES];
		[colm setSortDescriptorPrototype:sorter];
		[_tableView addTableColumn:colm];
		// bind column’s value to entries.arrangedObjects.(tag)
		[colm bind:@"value" toObject:entries withKeyPath:[NSString stringWithFormat:@"arrangedObjects.%@",tag] options:nil];
//NSLog(@"%@ column binding to %@", colDef[@"header"], [NSString stringWithFormat:@"arrangedObjects.%@",tag]);
	}
// say _tableView’s sortDescriptors are bound to (NCArrayController *)entries.sortDescriptors…
// then shouldn’t we init those sortDescriptors?

	// load column order/sorting
	[_tableView setAutosaveName:[NSString stringWithFormat:@"%@-soup", self.soup.name]];

	// we want to be able to drag elsewhere (export)
	[_tableView setDraggingSourceOperationMask:NSDragOperationCopy forLocal:NO];
	[_tableView setDraggingSourceOperationMask:NSDragOperationNone forLocal:YES];
	// and accept drops (import) of file types appropriate for this soup
	[_tableView registerForDraggedTypes:@[(NSString *)kUTTypeFileURL, kDataTypeSoupEntry]];
	[_tableView setDraggingDestinationFeedbackStyle:NSTableViewDraggingDestinationFeedbackStyleNone];	// ?
	isRegisteredForDraggedTypes = YES;

	[NSNotificationCenter.defaultCenter addObserver:self
														  selector:@selector(operationDone:)
																name:kDockDidOperationNotification
															 object:self.document];
}


- (void)viewWillDisappear
{
	[_tableView unregisterDraggedTypes];
	[NSNotificationCenter.defaultCenter removeObserver:self];
	[super viewWillDisappear];
}


/* -----------------------------------------------------------------------------
	Enable main menu items as per app logic.
		Edit > Delete			enable if we have a selection
		Edit > Select All		let the system handle it
	Args:		inItem
	Return:	YES => enable
----------------------------------------------------------------------------- */

- (BOOL)validateMenuItem:(id<NSValidatedUserInterfaceItem>)inItem
{
	if ([inItem action] == @selector(delete:)) {
		if (![self.document isKindOfClass: [NBDocument class]]
		&&  entries.selectionIndex != NSNotFound) {
			// we have an NCX2 document and a selection
			if (gNCNub.isTethered
			||  !self.soup.app.isPackages) {
				// we are not looking at archived packages
				return YES;
			}
		}
	}
	return NO;
}


/* -----------------------------------------------------------------------------
	Delete selected soup entries.
	Args:		--
	Return:	--
----------------------------------------------------------------------------- */

- (IBAction)delete:(id)sender
{
	NSIndexSet * selection = [entries selectionIndexes];
	if (selection.count > 0) {
		// we actually have selected some entries
		NSUserDefaults * defaults = NSUserDefaults.standardUserDefaults;
		if (!gNCNub.isTethered && ![defaults boolForKey: kNoDeleteWarningPref])
		{
			// user still wants to see this reminder
			NSAlert * alert = [[NSAlert alloc] init];
			[alert addButtonWithTitle: NSLocalizedString(@"delete", nil)];
			[alert addButtonWithTitle: NSLocalizedString(@"cancel", nil)];
			[alert setMessageText: @"You are not connected to a Newton device."];
			[alert setInformativeText:@"Soup entries will be deleted from your Newton device when you back up next."];
			[alert setShowsSuppressionButton:YES];
			[alert setAlertStyle: NSAlertStyleWarning];
			NSButton * suppressionButton = [alert suppressionButton];
			[suppressionButton setTitle:@"Don’t show this again."];
//			[suppressionButton setState:NSOffState];	// it’s the default

			NSInteger result = [alert runModal];

			if ([suppressionButton state] == NSOnState)
				[defaults setBool:YES forKey: kNoDeleteWarningPref];
			alert = nil;
			if (result != NSAlertFirstButtonReturn)
				return;	// cancel deletion
		}

		if (gNCNub.isTethered) {
			if (self.soup.app.isPackages) {
				// packages are deleted by name
				NSMutableArray * nameList = [NSMutableArray array];
				void (^lister)(NSUInteger idx, BOOL * stop) = ^(NSUInteger idx, BOOL * stop) {
					NCEntry * entry = [[entries arrangedObjects] objectAtIndex: idx];
					// extract unique name from pkg binary
					NSString * pkgName = PackageName(entry.ref);
					if (pkgName)
						[nameList addObject:pkgName];
				};
				[selection enumerateIndexesUsingBlock: lister];
NSLog(@"will delete %@",nameList);
				[gNCNub deletePackages:nameList onStore:self.soup.store];
			} else {
				// soup entries are deleted by _uniqueId
				NSMutableIndexSet * idList = [NSMutableIndexSet indexSet];
				void (^lister)(NSUInteger idx, BOOL * stop) = ^(NSUInteger idx, BOOL * stop)
				{
					NCEntry * entry = [[entries arrangedObjects] objectAtIndex: idx];
					[idList addIndex:[entry.uniqueId unsignedIntValue]];
				};
				[selection enumerateIndexesUsingBlock: lister];
				[gNCNub deleteEntries:idList from:soup];
			}
		}
		[entries removeObjectsAtArrangedObjectIndexes: selection];
	}
}


/* -----------------------------------------------------------------------------
	Import soup entries.
	Args:		sender
	Return:	--
----------------------------------------------------------------------------- */

- (void)import:(NSArray *)inURLs
{
	if ([self.document isKindOfClass:[NBDocument class]])
		return;

	NSUserDefaults * defaults = NSUserDefaults.standardUserDefaults;
	if (!gNCNub.isTethered && ![defaults boolForKey:kNoAddWarningPref]) {
		// user still wants to see this reminder
		NSAlert * alert = [[NSAlert alloc] init];
		[alert addButtonWithTitle: NSLocalizedString(@"ok", nil)];
		[alert addButtonWithTitle: NSLocalizedString(@"cancel", nil)];
		[alert setMessageText: @"You are not connected to a Newton device."];
		[alert setInformativeText:@"Soup entries will be added to your Newton device when you sync next."];
		[alert setShowsSuppressionButton:YES];
		[alert setAlertStyle: NSAlertStyleWarning];
		NSButton * suppressionButton = [alert suppressionButton];
		[suppressionButton setTitle:@"Don’t show this again."];
//		[suppressionButton setState:NSOffState];	// it’s the default

		NSInteger result = [alert runModal];

		if ([suppressionButton state] == NSOnState)
			[defaults setBool:YES forKey: kNoAddWarningPref];
		alert = nil;
		if (result != NSAlertFirstButtonReturn)
			return;	// cancel import
	}

	// import into local soup object
	NSMutableArray * newEntries = [NSMutableArray arrayWithCapacity:[inURLs count]];
	NCXPlugInController * plugin = [NCXPlugInController sharedController];
	newton_try
	{
		for (NSURL * url in inURLs) {
			RefVar ref;
			NCSoup * soupObj = soup;
			[plugin beginImport: self.soup.app context: self.document source: url];
			while (NOTNIL(ref = [plugin import])) {
			// for Dates, the soup may change depending on the kind of meeting (repeating or no, etc)
			// so we should ask the plugin for the soup name first, and change to it as necessary
				if (![soupObj.name isEqualToString:plugin.importSoupName])
					soupObj = [soupObj.store findSoup:plugin.importSoupName];
				if (soupObj == nil) {
					NSLog(@"-[NCSoupInfoController import] could not find %@ soup on %@ store", plugin.importSoupName, self.soup.store.name);
					break;
				}
				NCEntry * entryObj = [soupObj addEntry:ref];
				if (entryObj) {
					[newEntries addObject:entryObj];
				}
				else {
					NSLog(@"-[NCSoupInfoController import] failed to add imported entry to %@ soup:", soupObj.name);
					PrintObject(ref, 0);
				}
			}
			[plugin endImport];
		}
	}
	newton_catch_all
	{
		NewtonErr err = (NewtonErr)(long)CurrentException()->data;
		NSLog(@"\n#### Exception %s (%d) during import.", CurrentException()->name, err);
	}
	end_try;

	// import into Newton
	if (newEntries.count > 0) {
		[gNCNub importEntries:newEntries];
	}
}


/* -----------------------------------------------------------------------------
	The import operation is done.
----------------------------------------------------------------------------- */

- (void)operationDone:(NSNotification *)inNotification {

	NSNumber * err = inNotification.userInfo[@"error"];
	self.document.errorStatus = err.intValue;

	[_tableView reloadData];
}

@end
