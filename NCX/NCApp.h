/*
	File:		NCApp.h

	Abstract:	An NCApp models a Newton application.
					It contains a list of the soups used by that app.

	Written by:		Newton Research, 2012.
*/

#import <CoreData/CoreData.h>

#import "NCSoup.h"

@interface NCApp : NSManagedObject <NCSourceItem>
{
	BOOL isSelected;
}

@property(nonatomic,retain) NSString * name;
@property(nonatomic,retain) NSMutableSet * soups;

@property(nonatomic,readonly)	NSImage * image;
@property(nonatomic,readonly) NCInfoController * viewController;
@property(nonatomic,readonly)	BOOL isPackages;
@property(nonatomic,assign)	BOOL isSelected;

- (BOOL)isEqualTo:(NCApp *)inApp;

@end


@interface NCApp (CoreDataGeneratedAccessors)

- (void)addSoupsObject:(NCSoup *)value;
- (void)removeSoupsObject:(NCSoup *)value;
- (void)addSoups:(NSSet *)value;
- (void)removeSoups:(NSSet *)value;

@end

