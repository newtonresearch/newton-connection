/*
	File:		NCXPassthruView.h

	Contains:	NCXPassthruView that implements the NSTextInputClient protocol.
					It accepts keyDowns, maps them to Newton key codes,
					and sends them to the Newton device.

	Written by:	Newton Research Group, 2007.
*/

@protocol NCPassthru
- (void)passthruText:(NSString *)inText;
@end


@interface NCPassthruView : NSView<NSTextInputClient>

@property IBOutlet id<NCPassthru> target;	// pass text input to KeyboardViewController

- (BOOL)acceptsFirstResponder;
- (void)keyDown:(NSEvent *)inEvent;
- (void)insertCharacter:(unichar)inCharacter;
@end
