/*
	PkgPart.h
	Newton package inspector.

	Written by Newton Research, 2008.
*/

#import <Cocoa/Cocoa.h>
#import "Newton/PackageParts.h"

/* -----------------------------------------------------------------------------
	P k g I n f o
	Overall package info.
----------------------------------------------------------------------------- */
@class PkgPart;

@interface PkgInfo : NSObject
@property(readonly) NSString * ident;
@property(readonly) NSString * version;
@property(readonly) NSString * copyright;
@property(readonly) NSString * name;
@property(readonly) NSString * size;
@property(readonly) NSString * creationDate;
@property(readonly) BOOL isCopyProtected;
@property(readonly) NSMutableArray * parts;

- (id)initWithDirectory:(const PackageDirectory	*)inPkgDirectory;
- (void)addPart:(PkgPart *)inPart;
@end


/* -----------------------------------------------------------------------------
	P k g P a r t
	The base class for all package parts.
	This is fine for all non-specialized parts, eg auto, custom.
	It holds display information for the part.
----------------------------------------------------------------------------- */

@interface PkgPart : NSObject {
	NSImage * _iconImage;
}
@property(readonly) unsigned int partType;
@property(readonly) NSString * partTitle;
@property(readonly) NSString * size;
@property(readonly) NSImage * iconImage;
@property(readonly) Ref rootRef;
@property(readonly) char * data;

- (id)init:(const PartEntry *)inPart ref:(Ref)inRef data:(char *)inData sequence:(unsigned int)inSeq;
@end


/* -----------------------------------------------------------------------------
	P k g F o r m P a r t
	The application part.
----------------------------------------------------------------------------- */

@interface PkgFormPart : PkgPart
@property(readonly) NSString * text;
@end


/* -----------------------------------------------------------------------------
	P k g B o o k P a r t
	The book part.
----------------------------------------------------------------------------- */

@interface PkgBookPart : PkgPart
@property(readonly) NSString * title;
@property(readonly) NSString * isbn;
@property(readonly) NSString * author;
@property(readonly) NSString * copyright;
@property(readonly) NSString * date;
@end


/* -----------------------------------------------------------------------------
	P k g S o u p P a r t
	The soup part.
----------------------------------------------------------------------------- */

@interface PkgSoupPart : PkgPart
@end

