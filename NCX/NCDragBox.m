/*
	File:		NCDragBox.m

	Abstract:	Implementation of NCDragBox class.
					An NSBox that passes dragged items to a delegate.

	Written by:		Newton Research, 2012.
*/

#import "NCDragBox.h"


@interface NCDragBox ()
{
	IBOutlet id delegate;
}
@end


@implementation NCDragBox

- (NSDragOperation) draggingEntered: (id <NSDraggingInfo>) sender
{ return [delegate draggingEntered: sender]; }

- (BOOL) performDragOperation: (id <NSDraggingInfo>) sender
{ return [delegate performDragOperation: sender]; }

@end
