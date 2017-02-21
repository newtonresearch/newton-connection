/*
	File:		NCXPassthruView.m

	Contains:	NCPassthruView that implements the NSTextInputClient protocol.
					It accepts keyDowns, maps them to Newton key codes,
					and sends them to the Newton device.

	Written by:	Newton Research Group, 2007.
*/

#import <Cocoa/Cocoa.h>
#import "NCXPassthruView.h"


// mapping for Mac function characters in the range 0xF700..0xF71F -> Newton character codes
const UniChar newtCharMap[32] = 
{
	0x001E,0x001F,0x001C,0x001D,										// up, down, left, right
	0xF721,0xF722,0xF723,0xF724,0xF725,0xF726,0xF727,0xF728,	// function keys F1..F8
	0xF729,0xF72A,0xF72B,0xF72C,0xF72D,0xF72E,0xF72F			// function keys F9..F15
};


/*------------------------------------------------------------------------------
	N C P a s s t h r u V i e w
------------------------------------------------------------------------------*/

@implementation NCPassthruView

- (BOOL)acceptsFirstResponder {
	return YES;
}

- (void)keyDown:(NSEvent *)inEvent {
	NSString * str = inEvent.characters;
	if (str.length == 1) {
		unichar ch = [str characterAtIndex:0];
		// map control keys
		if (ch < 0x20) {
			[self insertCharacter:ch];
			return;
		} else if (ch == 0x7F) {					// delete
			[self insertCharacter:0x08];
			return;
		} else if ((ch & 0xFFE0) == 0xF700) {	// arrows, function keys
			[self insertCharacter:newtCharMap[ch & 0x1F]];
			return;
		}
	}
	[self interpretKeyEvents:@[inEvent]];
}

- (void)insertCharacter:(unichar)inCharacter {
	unichar ch = inCharacter;
	[self insertText:[NSString stringWithCharacters:&ch length:1] replacementRange:NSMakeRange(0,0)];
}

// NSTextInputClient protocol

- (nullable NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange {
	return nil;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)thePoint {
	return NSNotFound;
}

- (void)doCommandBySelector:(SEL)selector {
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange {
	return NSZeroRect;
}

- (BOOL)hasMarkedText {
	return NO;
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
	[self. target passthruText:string];
}

- (NSRange)markedRange {
	return self.hasMarkedText ? NSMakeRange(0,1) : NSMakeRange(NSNotFound,0);
}

- (NSRange)selectedRange {
	return NSMakeRange(0,0);
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange {
}

- (void)unmarkText {
}

- (NSArray<NSString *> *)validAttributesForMarkedText {
	return @[];
}

@end

