/*
	File:		NCSourceItem.m

	Abstract:	An NCSourceItem knows its own name.

	Written by:		Newton Research, 2012.
*/

#import "NCSourceItem.h"


@implementation NCSourceGroup

@synthesize name;

- (NSImage *)image {
	return nil;
}

- (void)setImage:(NSImage *)inImage {
}

- (NSString *) identifier {
	return @"Group";
}

- (NCInfoController *)viewController {
	return nil;
}

- (id)initGroup:(NSString *)inName {
	if (self = [super init]) {
		self.name = inName;
	}
	return self;
}

@end
