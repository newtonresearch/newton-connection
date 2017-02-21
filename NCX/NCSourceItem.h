/*
	File:		NCSourceItem.h

	Abstract:	An NCSourceItem knows its own name.

	Written by:		Newton Research, 2012.
*/

#import <Cocoa/Cocoa.h>

@class NCInfoController;

@protocol NCSourceItem
- (NSString *) name;
- (NSImage *) image;
- (NSString *) identifier;
@end


@interface NCSourceGroup : NSObject <NCSourceItem>
@property(nonatomic,strong) NSString * name;
@property(nonatomic,strong) NSImage * image;
@property(nonatomic,readonly) NSString * identifier;
@property(nonatomic,readonly) NCInfoController * viewController;

- (id)initGroup:(NSString *)inName;
@end

