/*
	File:		NCDragBox.h

	Abstract:	Interface for NCDragBox class.
					An NSBox that passes dragged items to a delegate.

	Written by:		Newton Research, 2012.
*/

#import <Cocoa/Cocoa.h>


@interface NCDragBox : NSBox

- (NSDragOperation) draggingEntered: (id <NSDraggingInfo>) sender;
- (BOOL) performDragOperation: (id <NSDraggingInfo>) sender;

@end
