/*
	File:		InfoController.mm

	Abstract:	Implementation of NCInfoController subclasses.

	Written by:		Newton Research, 2011.
*/

#import "BackupDocument.h"
#import "InfoController.h"


/* -----------------------------------------------------------------------------
	N C I n f o C o n t r o l l e r
	A generic Info view is essentially static.
----------------------------------------------------------------------------- */

@implementation NCInfoController

- (void)viewDidLoad
{
	[super viewDidLoad];
	isRegisteredForDraggedTypes = NO;
}

@end
