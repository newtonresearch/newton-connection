/*
	File:		NRProgressBox.m

	Contains:	A Yosemite-style (Safari, Xcode) progress box:
					round rect, filled white, thin blue progress gauge.

	Written by:	Newton Research Group, 2014.
*/

#import "NRProgressBox.h"


/* -----------------------------------------------------------------------------
	N R P r o g r e s s B o x

	No subviews here - we draw everything as needed.
	To update the box, set the properties required and set needsDisplay = YES
	TODO: Draw dimmed state?
----------------------------------------------------------------------------- */
@implementation NRProgressBox

- (void)drawRect:(NSRect)inRect {
	[NSGraphicsContext saveGraphicsState];

//	fill round rect with white
	NSRect bounds = NSInsetRect(self.bounds, 0.0, 1.0);

	NSBezierPath * path = [NSBezierPath bezierPathWithRoundedRect:bounds xRadius:3.5 yRadius:3.5];
	[self.borderColor setStroke];
	[path stroke];
	[self.fillColor setFill];
	[path fill];
// apply shadow?

// clip to round rect
	[path setClip];

	BOOL isActive = NO;

//	if progress bar required, fill with blue
	if (0.0 <= self.barValue && self.barValue <= 1.0) {
		NSRect bar = bounds;
		bar.size.height = 2.0;
		bar.size.width *= self.barValue;
		[[NSColor colorWithCalibratedRed:0.086274 green:0.494117 blue:0.984313 alpha:1.0] setFill];
		NSRectFill(bar);
		isActive = YES;
// we could draw the cancel button here rather than use an NSButton in the storyboard as we do at present
//		if (self.canCancel) {
//		// draw cancel button : if pressed, highlight it
//		}
	}

// write status text
	if (self.statusText && self.statusText.length > 0) {
		NSRect box = NSInsetRect(bounds, 10.0, 3.0);
		box.origin.y += 4.0;
		[self.statusText drawWithRect:box options:0 attributes:@{ NSFontAttributeName:[NSFont systemFontOfSize:11.0],
																					 NSForegroundColorAttributeName:NSColor.blackColor }];
		isActive = YES;
	}

	if (!isActive) {
	// draw Newton logo
		NSRect box = bounds;
		box.origin.x += box.size.width/2.0 - 11.0;
		box.origin.y = 2.0;
		box.size.width = 22.0;
		box.size.height = 22.0;
		[[NSImage imageNamed:@"logo"] drawInRect:box];
	}

	[NSGraphicsContext restoreGraphicsState];
}

@end

