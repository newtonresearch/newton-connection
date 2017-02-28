/*
	File:		NCStore.m

	Abstract:	An NCStore models a Newton store.
					It contains info about the store, and lists of the apps and soups on that store.

	Written by:		Newton Research, 2012.
*/

#import "NCStore.h"

#define KByte 1024

extern NSNumberFormatter * gNumberFormatter;


@implementation NCStore

- (NSString *) identifier {
	return @"Store";
}

@dynamic name;
@dynamic kind;
@dynamic readOnly;
@dynamic defaultStore;
@dynamic signature;
@dynamic storePassword;
@dynamic storeVersion;
@dynamic usedSize;
@dynamic totalSize;
@dynamic soups;

/* -----------------------------------------------------------------------------
	Compare stores.
----------------------------------------------------------------------------- */

- (BOOL)isEqualTo:(NCStore *)inStore {
	return [self.name isEqualToString:inStore.name]
		 && [self.signature isEqualTo:inStore.signature];
}


/* -----------------------------------------------------------------------------
	Generate UI representation.
----------------------------------------------------------------------------- */

- (NSImage *)image {
	return [NSImage imageNamed: [self.kind isEqualToString: @"Internal"] ? @"source-internal.png" : @"source-store.png"];
}


- (NSString *)icon {
	NSString * iconStr;
	if ([self.kind isEqualToString: @"Internal"])
		iconStr = @"internalStore.png";
	else
		iconStr = @"cardStore.png";
	return [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent: iconStr];
}


- (NSString *)status {
	NSString * statusStr = nil;
	if (self.isDefault)
		statusStr = @"Default store";
	if ([self.storePassword length] > 0)
	{
		if (statusStr)
			statusStr = [NSString stringWithFormat: @"%@, password protected.", statusStr];
		else
			statusStr = @"Password protected.";
	}
	if (statusStr == nil)
		statusStr = @"--";
	return statusStr;
}


- (NSString *)capacity {
	return [NSString stringWithFormat: @"%@K  (%@ bytes)", [gNumberFormatter stringFromNumber: [NSNumber numberWithInt: [self.totalSize intValue] / KByte]],
																			 [gNumberFormatter stringFromNumber: self.totalSize]];
}

- (NSString *)used {
	return [NSString stringWithFormat: @"%@K  (%@ bytes)", [gNumberFormatter stringFromNumber: [NSNumber numberWithInt: [self.usedSize intValue] / KByte]],
																			 [gNumberFormatter stringFromNumber: self.usedSize]];
}

- (NSString *)free {
	int num = [self.totalSize intValue] - [self.usedSize intValue];
	return [NSString stringWithFormat: @"%@K  (%@ bytes)", [gNumberFormatter stringFromNumber: [NSNumber numberWithInt: num / KByte]],
																			 [gNumberFormatter stringFromNumber: [NSNumber numberWithInt: num]]];
}

- (BOOL)isReadOnly {
	return [self.readOnly boolValue];
}

- (BOOL)isInternal {
	return [self.kind isEqualToString:@"Internal"];
}

- (BOOL)isDefault {
	return [self.defaultStore boolValue];
}


/* -----------------------------------------------------------------------------
	Find a persistent soup object on this store
	Args:		inStore
				inName
	Return:	NCSoup instance
----------------------------------------------------------------------------- */

- (NCSoup *)findSoup:(NSString *)inName {
	NSManagedObjectContext * objContext = [self managedObjectContext];
	NSFetchRequest * request = [[NSFetchRequest alloc] init];
	[request setEntity:[NSEntityDescription entityForName:@"Soup" inManagedObjectContext:objContext]];
	[request setPredicate:[NSPredicate predicateWithFormat:@"name = %@ AND store = %@", inName, self]];

	NSError *__autoreleasing error = nil;
	NSArray * results = [objContext executeFetchRequest:request error:&error];

	NCSoup * soup = nil;
	if ([results count] > 0)
		soup = [results objectAtIndex:0];
	return soup;
}


/* -----------------------------------------------------------------------------
	Find existing packages on this store.
	Args:		--
	Return:	NSArray of NCEntry*s, sorted alphabetically by name
----------------------------------------------------------------------------- */

- (NSArray *)pkgs {
	NSManagedObjectContext * objContext = [self managedObjectContext];
	NSFetchRequest * request = [[NSFetchRequest alloc] init];
	[request setEntity:[NSEntityDescription entityForName:@"Entry" inManagedObjectContext:objContext]];
	[request setPredicate:[NSPredicate predicateWithFormat:@"soup.store = %@ AND soup.app.name = 'Packages'", self]];

	NSSortDescriptor * sorter = [[NSSortDescriptor alloc] initWithKey:@"title" ascending:YES];
	[request setSortDescriptors:[NSArray arrayWithObject:sorter]];

	NSError *__autoreleasing error = nil;
	NSArray * results = [objContext executeFetchRequest:request error:&error];

	for (NCEntry * pkg in results)
	{
		pkg.isSelected = YES;
	}

	return results;
}


/* -----------------------------------------------------------------------------
	Find existing apps on this store.
	Args:		includePkgs		YES => include Packages as an app
	Return:	NSArray of NCApp*s, sorted alphabetically by name
----------------------------------------------------------------------------- */

- (NSArray *)apps:(BOOL)includePkgs {
	NSManagedObjectContext * objContext = [self managedObjectContext];
	NSFetchRequest * request = [[NSFetchRequest alloc] init];
	[request setEntity:[NSEntityDescription entityForName:@"App" inManagedObjectContext:objContext]];

	NSString * fmt = includePkgs ? @"ANY soups.store = %@"
										  : @"ANY soups.store = %@ AND name != 'Packages'";
	[request setPredicate:[NSPredicate predicateWithFormat:fmt, self]];

	NSSortDescriptor * sorter = [[NSSortDescriptor alloc] initWithKey:@"name" ascending:YES];
	[request setSortDescriptors:[NSArray arrayWithObject:sorter]];

	NSError *__autoreleasing error = nil;
	NSArray * results = [objContext executeFetchRequest:request error:&error];

	for (NCApp * app in results)
	{
		app.isSelected = YES;
	}

	return results;
}


/* -----------------------------------------------------------------------------
	Find soups owned by an app on this store.
	Args:		inStore
				inApp
	Return:	NSArray of NCSoup instances
----------------------------------------------------------------------------- */

- (NSArray *)soupsInApp:(NCApp *)inApp {
	NSManagedObjectContext * objContext = [self managedObjectContext];
	NSFetchRequest * request = [[NSFetchRequest alloc] init];
	[request setEntity:[NSEntityDescription entityForName:@"Soup" inManagedObjectContext:objContext]];
	[request setPredicate:[NSPredicate predicateWithFormat:@"app = %@ AND store = %@", inApp, self]];

	NSError *__autoreleasing error = nil;
	NSArray * results = [objContext executeFetchRequest:request error:&error];

	return results;
}

@end
