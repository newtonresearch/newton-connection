/*
	File:		NSString-Extensions.h

	Contains:	NSString support category declarations for Newton Connection.

	Written by:	Newton Research Group, 2007.
*/

#import <Foundation/NSString.h>

@interface NSString (NCXExtensions)
+ (BOOL) isEmptyString: (NSString *) string;
- (BOOL) containsCharacterInSet: (NSCharacterSet *) searchSet;
- (NSString *) stringByReplacingAllOccurrencesOfString: (NSString *) stringToReplace withString: (NSString *) replacement;
- (NSString *) stringByReplacingCharactersInSet: (NSCharacterSet *) set withString: (NSString *) replaceString;
@end

@interface NSMutableString (NCXExtensions)
- (void) replaceAllOccurrencesOfCharactersInSet: (NSCharacterSet *) set withString: (NSString *) replaceString;
@end

