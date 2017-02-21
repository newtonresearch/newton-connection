/*
	File:		NCUserInfo.h

	Abstract:	An NCUserInfo models a Newton user’s preferences.

	Written by:		Newton Research, 2012.
*/

#import <AppKit/AppKit.h>
#import <CoreData/CoreData.h>

@interface NCUserInfo : NSManagedObject
{
}

@property(nonatomic,retain) NSFont * font;
@property(nonatomic,retain) NSDictionary * folders;

@end
