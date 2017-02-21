/*
	File:		NCEntry.mm

	Abstract:	An NCEntry models a Newton soup entry.
					It contains the entry frame.

	Written by:		Newton Research, 2012.
*/

#import "NCXPlugIn.h"
#import "PlugInUtilities.h"
#import "NCSlot.h"
#import "NCEntry.h"
#import "NCSoup.h"


@implementation NCSlot
- (void) setRef:(Ref)ref	{ theSlot = ref; }
- (Ref) ref						{ return theSlot; }
@end


#pragma mark - NCEntry soup entry representation
/* -----------------------------------------------------------------------------
	N C E n t r y
----------------------------------------------------------------------------- */
@interface NCEntry ()
{
	id _info1;
	id _info2;
	NSString * _labels;
}
- (id)transformSlot:(NSUInteger)inSlot;
@end


@implementation NCEntry

@dynamic refData;
@dynamic refClass;
@dynamic title;
@dynamic uniqueId;
@dynamic modTime;
@dynamic soup;

@synthesize isSelected;

/* -----------------------------------------------------------------------------
	NSPasteboardItemDataProvider protocol
----------------------------------------------------------------------------- */

- (void)pasteboard:(NSPasteboard *)inPasteboard item:(NSPasteboardItem *)inItem provideDataForType:(NSString *)inType
{
	if ([inType compare:kDataTypeSoupEntry] == NSOrderedSame) {
		//set data for soup entry type on the pasteboard as requested
	  [inPasteboard setData:self.refData forType:kDataTypeSoupEntry];
	} else if ([inType compare:(__bridge NSString *)kPasteboardTypeFilePromiseContent] == NSOrderedSame) {
		// pasteboard asks for type of file we will generate
		[inPasteboard setString:[[NCXPlugInController sharedController] typeForClass:self.refClass] forType:(__bridge NSString *)kPasteboardTypeFilePromiseContent];
	} else if ([inType compare:(__bridge NSString *)kPasteboardTypeFileURLPromise] == NSOrderedSame) {
		// pasteboard asks for promised file
		// we will never see this -- file is created in -[NCArrayController tableView:namesOfPromisedFilesDroppedAtDestination:forDraggedRowsWithIndexes:]
NSLog(@"-pasteboard:item:provideDataForType:%@",inType);
	}
}


- (void)pasteboardFinishedWithDataProvider:(NSPasteboard *)inPasteboard
{
//NSLog(@"-[NCEntry pasteboardFinishedWithDataProvider:]");
}


/* -----------------------------------------------------------------------------
	The labels slot is used by Newton for filing.
	Args:		--
	Return:	NSString*
----------------------------------------------------------------------------- */
NSDictionary * gUserFolders;

- (id) labels
{
	if (_labels == nil) {
		_labels = [self transformSlot:0];
	}
	return _labels;
}


/* -----------------------------------------------------------------------------
	There are two optional columns in the soup entries table view, info1 and info2,
	forming part of the whole list
		id - title - info1 - info2 - date
	Args:		--
	Return:	NSString* or NSDate*
----------------------------------------------------------------------------- */

- (id) info1
{
	if (_info1 == nil) {
		_info1 = [self transformSlot:2];
	}
	return _info1;
}


- (id) info2
{
	if (_info2 == nil) {
		_info2 = [self transformSlot:3];
	}
	return _info2;
}


- (id) transformSlot: (NSUInteger) inSlot
{
	static RefStruct * entryRef = nil;
	static NCEntry * entryCache = nil;
	if (entryCache != self) {
		CPtrPipe pipe;
		pipe.init((void *)self.refData.bytes, self.refData.length, NO, nil);
		if (entryRef == nil)
			entryRef = new RefStruct;
		*entryRef = UnflattenRef(pipe);
		entryCache = self;
	}

	id str;
	if (inSlot == 0) {
		RefVar labelSlot(GetFrameSlot(*entryRef, SYMA(labels)));
		if (IsSymbol(labelSlot)) {
		// map symbol -> string using document’s userFolders dictionary
			NSString * tag = [NSString stringWithCString:SymbolName(labelSlot) encoding:NSMacOSRomanStringEncoding];
			NSString * label = [gUserFolders objectForKey:tag];
//NSLog(@"labels: %@ -> %@", tag, label);
			str = label ? label : tag;
		} else {
			str = @"Unfiled";
		}
	} else {

		NSDictionary * infoDict = [self.soup.columnInfo objectAtIndex:inSlot];
		RefVar slot(GetFrameSlot(*entryRef, MakeSymbol([[infoDict objectForKey:@"slot"] UTF8String])));
		// transform it
		NCSlot * slotObj = [[NCSlot alloc] init];
		SEL transform = NSSelectorFromString([NSString stringWithFormat:@"transform%@:",[[infoDict objectForKey:@"type"] capitalizedString]]);
		if ([self respondsToSelector: transform]) {
			slotObj.ref = slot;
			str = [self performSelector:transform withObject: slotObj];
		} else if (IsString(slot)) {
			str = MakeNSString(slot);
		} else {
			str = @"";
		}
	}
	return str;
}


// transforms:
//  date
//  nameFrame
//  integer (GMT)
//  symbol
//  label

- (NSDate *) transformDateref: (NCSlot *) inSlot
{
	return MakeNSDate(inSlot.ref);
}



- (NSString *) transformNamerefarray: (NCSlot *) inSlot
{
	if (IsArray(inSlot.ref) && Length(inSlot.ref) > 0)
	{
		NCSlot * slotObj = [[NCSlot alloc] init];
		slotObj.ref =  GetFrameSlot(GetArraySlot(inSlot.ref, 0), SYMA(name));
		return [self transformName:slotObj];
	}
	return @"";
}


- (NSString *) transformName: (NCSlot *) inSlot
{
	// honorific, first, last
	NSMutableArray * parts = [NSMutableArray arrayWithCapacity:3];
	RefVar item = GetFrameSlot(inSlot.ref, MakeSymbol("honorific"));
	if (NOTNIL(item))
		[parts addObject: MakeNSString(item)];
	item = GetFrameSlot(inSlot.ref, SYMA(first));
	if (NOTNIL(item))
		[parts addObject: MakeNSString(item)];
	item = GetFrameSlot(inSlot.ref, SYMA(last));
	if (NOTNIL(item))
		[parts addObject: MakeNSString(item)];
	return [parts componentsJoinedByString:@" "];
}


- (NSString *) transformGmt: (NCSlot *) inSlot
{
	int valu = (int)RINT(inSlot.ref);
	// valu is in SECONDS, despite what the docs say, so convert to minutes
	valu /= 60;
	// we want to end up with a string in the format +01:00, or blank if we have GMT
	if (valu == 0)
		return @"";
	if (valu > 0)
		return [NSString stringWithFormat:@"+%02d:%02d",valu/60,valu%60];
	valu = -valu;
	return [NSString stringWithFormat:@"-%02d:%02d",valu/60,valu%60];
}


- (NSString *) transformSymbol: (NCSlot *) inSlot
{
	return [NSString stringWithCString:SymbolName(inSlot.ref) encoding:NSMacOSRomanStringEncoding];
}

@end
