/*
	File:		NCSlot.h

	Abstract:	An NCSlot allows you to pass a Newton Ref as an Obj-C arg.

	Written by:		Newton Research, 2014.
*/

@interface NCSlot : NSObject
{
	RefStruct theSlot;
}
@property(assign) Ref ref;
@end
