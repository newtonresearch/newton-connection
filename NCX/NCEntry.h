/*
	File:		NCEntry.h

	Abstract:	An NCEntry models a Newton soup entry.
					It contains the entry frame.

	Written by:		Newton Research, 2012.
*/

#import <Cocoa/Cocoa.h>

@class NCSoup;

@interface NCEntry : NSManagedObject <NSPasteboardItemDataProvider>
@property(nonatomic,retain) NSData * refData;
@property(nonatomic,retain) NSString * refClass;
@property(nonatomic,retain) NSString * title;
@property(nonatomic,retain) NSNumber * uniqueId;
@property(nonatomic,retain) NSDate * modTime;
@property(nonatomic,retain) NCSoup * soup;

@property(nonatomic,readonly) id labels;
// transient properties for table view -- will be NSString* or NSDate*
// soup needs to know how these properties are derived
@property(nonatomic,readonly) id info1;
@property(nonatomic,readonly) id info2;

@property(nonatomic,assign) BOOL isSelected;

// NSPasteboardItemDataProvider protocol
//- (void)pasteboard:(NSPasteboard *)inPasteboard item:(NSPasteboardItem *)inItem provideDataForType:(NSString *)inType;
//- (void)pasteboardFinishedWithDataProvider:(NSPasteboard *)inPasteboard;
@end

#define kDataTypeSoupEntry @"com.newton.entry"

#define kPackageRefClass @"*package*"