/*
	File:		NCWindowController.h

	Abstract:	Interface for NCWindowController class.

	Written by:		Newton Research, 2012.
*/

#import "SourceListViewController.h"
#import "InspectorViewController.h"
#import "DeviceViewController.h"
#import "KeyboardViewController.h"


/* -----------------------------------------------------------------------------
	N C W i n d o w C o n t r o l l e r
	Controller for the connection info window.
----------------------------------------------------------------------------- */

@interface NCWindowController : NSWindowController

// source split view controller (split view containing source list)
@property NCSourceSplitViewController * sourceSplitController;
// content split view controller (split view containing TellUser text)
@property NCInspectorSplitViewController * inspectorSplitController;
// document editor content controller
@property NSViewController * contentController;
@property NCSourceListViewController * sourceListController;
@property NCInspectorViewController * inspector;

// Connection status
@property(nonatomic,strong,readonly) NCDeviceViewController * deviceViewController;
@property(nonatomic,strong) NSProgress * overallProgress;
@property(nonatomic,strong) NSProgress * subProgress;
@property(nonatomic,assign) NSString * progressText;		// updating this property will update the window’s progress box

// sidebar
- (void)populateSourceList;
- (void)refreshStore:(NCStore *)inStore app:(NCApp *)inApp;
- (void)sourceSelectionDidChange:(id<NCSourceItem>)item;

- (void)performSync;			// for Newton 1; show progress gauge and simulate Sync button click

// progress display
- (void)connected:(NCDockProtocolController *)inDock;
- (void)disconnected:(NCDockProtocolController *)inDock;

- (void)startProgressFor:(id<NCComponentProtocol>)inHandler;
- (void)stopProgress;

// cancellation -- any cancel button should take this action
- (IBAction)cancelOperation:(id)sender;

@end

