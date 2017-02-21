/*
	File:		SoupViewController.h

	Abstract:	Interface for NBSoupInfoController class.

	Written by:		Newton Research, 2010.
*/

#import "InfoController.h"
#import "NCArrayController.h"
#import "NCSoup.h"


/* -----------------------------------------------------------------------------
	N C S o u p V i e w C o n t r o l l e r
	Controller for the soup info pane.
----------------------------------------------------------------------------- */

@interface NCSoupViewController : NCInfoController
{
	IBOutlet NCArrayController * _entries;
	IBOutlet NSTableView * _tableView;
}
@property(readonly) NCSoup * soup;
@property(readonly) NCArrayController * entries;

- (void) import: (NSArray *) inURLs;

@end
