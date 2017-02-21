/*
	File:		NSString-Extensions.m

	Contains:	NSString support category declarations for Newton Connection.

	Written by:	Newton Research Group, 2007.
*/

#import "NSString-Extensions.h"


@implementation NSString (NCXExtensions)

+ (BOOL) isEmptyString: (NSString *) string
{
    // Note that [string length] == 0 can be false when [string isEqualToString:@""] is true, because these are Unicode strings.
    return string == nil || [string isEqualToString:@""];
}


- (NSString *) stringByReplacingAllOccurrencesOfString: (NSString *) stringToReplace withString: (NSString *) replacement
{
	NSRange searchRange = NSMakeRange(0, [self length]);
	NSRange foundRange = [self rangeOfString:stringToReplace options:0 range:searchRange];

	// If stringToReplace is not found, then there's nothing to replace -- just return self
	if (foundRange.length == 0)
		return [self copy];

	NSMutableString *copy = [self mutableCopy];
	NSUInteger replacementLength = replacement.length;

	while (foundRange.length > 0)
	{
		[copy replaceCharactersInRange:foundRange withString:replacement];

		searchRange.location = foundRange.location + replacementLength;
		searchRange.length = [copy length] - searchRange.location;

		foundRange = [copy rangeOfString:stringToReplace options:0 range:searchRange];
	}

	return [copy copy];
}


- (BOOL) containsCharacterInSet: (NSCharacterSet *) searchSet
{
	NSRange characterRange;

	characterRange = [self rangeOfCharacterFromSet:searchSet];
	return characterRange.length != 0;
}


- (NSString *) stringByReplacingCharactersInSet: (NSCharacterSet *) set withString: (NSString *) replaceString
{
	NSMutableString *nooString;

	if (![self containsCharacterInSet:set])
		return self;
	nooString = [self mutableCopy];
	[nooString replaceAllOccurrencesOfCharactersInSet:set withString:replaceString];
	return nooString;
}

@end


@implementation NSMutableString (NCXExtensions)

- (void) replaceAllOccurrencesOfCharactersInSet: (NSCharacterSet *) set withString: (NSString *) replaceString
{
	NSRange characterRange, searchRange = NSMakeRange(0, self.length);
	NSUInteger replaceStringLength = replaceString.length;
	while ((characterRange = [self rangeOfCharacterFromSet:set options:NSLiteralSearch range:searchRange]).length)
	{
		[self replaceCharactersInRange:characterRange withString:replaceString];
		searchRange.location = characterRange.location + replaceStringLength;
		searchRange.length = [self length] - searchRange.location;
		if (searchRange.length == 0)
			break; // Might as well save that extra method call.
	}
}

@end

