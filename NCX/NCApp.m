/*
	File:		NCApp.m

	Abstract:	An NCApp models a Newton application.
					It contains a list of the soups used by that app.

	Written by:		Newton Research, 2012.
*/

#import "NCApp.h"


@implementation NCApp

@dynamic name;
@dynamic soups;

@synthesize isSelected;

- (BOOL)isEqualTo:(NCApp *)inApp {
	return [self.name isEqualToString:inApp.name];
}

- (BOOL) isPackages {
	return [self.name isEqualToString:@"Packages"];
}

- (NSImage *) image {
	return [NSImage imageNamed: @"source-app.png"];
}

- (NSString *) identifier {
	return @"App";
}

- (NCInfoController *) viewController {
	return nil;
}


@end
