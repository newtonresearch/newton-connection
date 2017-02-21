/*
	File:		NRProgressBox.h

	Contains:	A Yosemite-style (Safari, Xcode) progress box:
					round rect, filled white, thin blue progress gauge.

	Written by:	Newton Research Group, 2014.
*/

#import <Cocoa/Cocoa.h>


@interface NRProgressBox : NSBox
@property(strong) NSString * statusText;
@property(assign) float barValue;	// 0.0 .. 1.0
@property(assign) BOOL canCancel;	// YES => show cancel button, respond to it
@end
