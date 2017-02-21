/*
	File:		NCXPlugIn.mm

	Contains:	Plugin controller for the NCX app.

	Written by:	Newton Research Group, 2005.
*/

#import "NCXPlugIn.h"
#import "Session.h"

#define kDefaultTranslator	@"*default*"
#define kTextTranslator		@"text"
#define kPackageTranslator	@"*package*"
#define kImageTranslator	@"*image*"


@interface NCXPlugInController (Private)
- (void) installPlugIns: (NSString *) inPath;
@end


/*------------------------------------------------------------------------------
	N C X P l u g I n C o n t r o l l e r
------------------------------------------------------------------------------*/

@implementation NCXPlugInController

+ (NCXPlugInController *)sharedController
{
	static NCXPlugInController * sharedController = nil;
	if (sharedController == nil) {
		sharedController = [[self alloc] init];
	}
	return sharedController;
}


/*------------------------------------------------------------------------------
	Install plugins to be found in the usual places:
		in the app folder
		in the system’s Application Support folder
		in the user’s Application Support folder
	searched in this order so that third-party plugins can override builtins.
	The key (no pun intended) dictionaries are:
		exportFormats: { person: { type: kUTTypeVCard, exporter: NameToVCard },
							  meeting: { type: "ics", exporter: DateToICal },
							  *default*: NCDefaultTranslator,
							  ... }
	and:
		importFormats: { Names: { public.vcard: { soup: "Names", importer: NameFromVCard } },
							  Dates: { com.apple.ical.ics: { soup: "Dates", importer: DateFromICal } },
							  Works: { public.plain-text: { soup: "Works", importer: WorksFromText }, public.rtf: { soup: "Works", importer: WorksFromRTF } }
							  ... }
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/

- (id)init {
	if (self = [super init]) {
		NSArray * paths;
		NSString * applicationSupportFolder;

		allFileFormats = [[NSMutableDictionary alloc] initWithCapacity:3];

		exportFormats = [[NSMutableDictionary alloc] initWithCapacity:20];
		importFormats = [[NSMutableDictionary alloc] initWithCapacity:20];

		// install built-in plugins
		exportFormats[kPackageTranslator] = @{@"exporter":[[NCPackageTranslator alloc] init], @"type":@"com.newton.package"};
		exportFormats[kImageTranslator] = @{@"exporter":[[NCImageTranslator alloc] init], @"type":@"public.tiff"};
		exportFormats[kTextTranslator] = @{@"exporter":[[NCTextTranslator alloc] init], @"type":@"public.plain-text"};
		exportFormats[kDefaultTranslator] = [[NCDefaultTranslator alloc] init];
		[self installPlugIns: [[NSBundle mainBundle] builtInPlugInsPath]];

		// install system-wide plugins
		paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSLocalDomainMask, YES);
		if (paths.count > 0) {
			applicationSupportFolder = [paths[0] stringByAppendingPathComponent:@"Newton Connection"];
			[self installPlugIns: [applicationSupportFolder stringByAppendingPathComponent:@"PlugIns"]];
		}

		// install user plugins
		paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
		if (paths.count > 0) {
			applicationSupportFolder = [paths[0] stringByAppendingPathComponent:@"Newton Connection"];
			[self installPlugIns: [applicationSupportFolder stringByAppendingPathComponent:@"PlugIns"]];
		}
	}
	return self;
}


/*------------------------------------------------------------------------------
	Install all the plugins found in a folder.
	Args:		inPath
	Return:	--
------------------------------------------------------------------------------*/

- (void)installPlugIns:(NSString *)inPath {
	NSString * pluginsFolder = [inPath stringByExpandingTildeInPath];
	NSArray * files = [[NSFileManager defaultManager] contentsOfDirectoryAtPath: pluginsFolder error: nil];
	NSEnumerator * fileIter = [files objectEnumerator];
	NSString * path;
	while ((path = [fileIter nextObject]) != nil) {
		NSBundle * bundle = [NSBundle bundleWithPath: [pluginsFolder stringByAppendingPathComponent: path]];
		if (bundle != nil) {
			NSDictionary * infoDict = [bundle infoDictionary];

			// add file type descriptions (if any) to our dictionary
			NSDictionary * typeDict = [infoDict objectForKey: @"NCFileTypes"];
			if (typeDict != nil) {
				[allFileFormats addEntriesFromDictionary: typeDict];
			}
			// must have a Newton app name
			NSString * appName = [infoDict objectForKey: @"NCApplication"];
			NSAssert(appName != nil, @"No application name in bundle info dictionary.");

			// if this plug-in exports Newton data, add that info to exportFormats
			NSDictionary * exportDict = [infoDict objectForKey: @"NCExportFormats"];
			if (exportDict != nil) {
				for (NSString * key in exportDict) {
					NSMutableDictionary * exportInfo = [NSMutableDictionary dictionaryWithCapacity:2];
					[exportInfo setDictionary:exportDict[key]];
					// replace exporter class name with export translator instance
					NSString * className = (NSString *) exportInfo[@"exporter"];
					Class txClass = [bundle classNamed:className];
					NSAssert2(txClass != nil, @"No class %@ in %@ bundle.", className, appName);
					[exportInfo setObject: [[txClass alloc] init] forKey: @"exporter"];

					exportFormats[key] = exportInfo;
				}
			}

			// if this plug-in imports Newton data, add that info to importFormats
			NSDictionary * importDict = [infoDict objectForKey: @"NCImportFormats"];
			if (importDict != nil) {
				NSMutableDictionary * importerDict = [importDict mutableCopy];
				for (NSString * key in importDict) {	// key is UTI
					NSMutableDictionary * importInfo = [importDict[key] mutableCopy];
					// convert importer class name to class instance
					NSString * className = (NSString *)importInfo[@"importer"];
					Class txClass = [bundle classNamed:className];
					NSAssert2(txClass != nil, @"No class %@ in %@ bundle.", className, appName);
					importInfo[@"importer"] = [[txClass alloc] init];
					importerDict[key] =importInfo;
				}
				importFormats[appName] = importerDict;
			}
		}
	}
}


/*------------------------------------------------------------------------------
	Return the user-visible title of a file type.
	Args:		inFileType		eg vcf
	Return:	file type name, eg vCard
------------------------------------------------------------------------------*/

- (NSString *)fileFormat:(NSString *)inFileType {
	NSString * format = allFileFormats[inFileType];
	return format ? format : inFileType;
}


#pragma mark Export

/*------------------------------------------------------------------------------
	Return the file type we can export for a soup entry’s class.
	Args:		--
	Return:	an auto-released string
------------------------------------------------------------------------------*/

- (NSString *)typeForClass:(NSString *)inClass {
	NSDictionary * ftype = exportFormats[[inClass lowercaseString]];
	if (ftype) {
		return ftype[@"type"];
	}
	// by default we export NewtonScript PrintObject() output to a text file
	return @"public.plain-text";
}


/*------------------------------------------------------------------------------
	Translate soup entry to desktop format.
	Args:		inEntry
				inDocument
				inPath
	Return:	name of file exported
				nil => this translator builds data to be saved on endExport
------------------------------------------------------------------------------*/

- (void)beginExport:(NCApp *)inApp context:(NCDocument *)inDocument destination:(NSURL *)inURL {
	theApp = inApp;
	theContext = inDocument;
	theURL = inURL;
	usedTranslators = [[NSMutableSet alloc] initWithCapacity:1];
}


- (NSString *)export:(NCEntry *)inEntry {
	NSDictionary * exportInfo;
	NCXTranslator * txlator = nil;
	NSString * className = nil;

	CPtrPipe pipe;
	pipe.init((void *)inEntry.refData.bytes, inEntry.refData.length, NO, NULL);
	RefVar entryRef(UnflattenRef(pipe));

	RefVar classSymbol(GetFrameSlot(entryRef, SYMA(class)));

	if (NOTNIL(classSymbol)) {
		className = [[NSString stringWithUTF8String: SymbolName(classSymbol)] lowercaseString];
	} else {
	//	entry has no class!!
	//	Examine it to see what it might be.
		RefVar n;
	// 1:  Packages
		if (FrameHasSlot(entryRef, MakeSymbol("pkgRef")))
			className = kPackageTranslator;

	// 2:  Names entries -- I wonder which ‘enhancement’ creates these?
		else if (NOTNIL(GetFrameSlot(entryRef, SYMA(company))))
			className = @"company";
		else if (NOTNIL(n = GetFrameSlot(entryRef, SYMA(name)))
			  &&  EQ(ClassOf(n), SYMA(person)))
			className = @"person";

	// 3:  BitMap image -- probably NewtPaint
		else if (IsFrame(GetFrameSlot(entryRef, MakeSymbol("image"))))
			className = kImageTranslator;

	// 4:  Plain text
		else if (NOTNIL(GetFrameSlot(entryRef, SYMA(text))))
			className = kTextTranslator;

	// 5:  TBC
	}

	if (className == nil
	|| (exportInfo = exportFormats[className]) == nil
	|| (txlator = exportInfo[@"exporter"]) == nil) {
		// unknown class ->PrintObject()
		txlator = exportFormats[kDefaultTranslator];
	}
	if (![usedTranslators containsObject:txlator]) {
		[usedTranslators addObject:txlator];
		[txlator beginExport:theApp.name context:theContext destination:theURL];
	}

	return [txlator export:entryRef];
}


- (NSString *)endExport {
	NSString *__block filename = nil;

	void (^finalizer)(id obj, BOOL *stop) = ^(id obj, BOOL * stop) {
		NSString * finame = [(NCXTranslator *)obj exportDone: theApp.name];
		if (finame)
			filename = [finame copy];
	};
	[usedTranslators enumerateObjectsUsingBlock: finalizer];

	usedTranslators = nil;
	theURL = nil;
	theContext = nil;
	theApp = nil;
	return filename;
}


#pragma mark Import

/*------------------------------------------------------------------------------
	Return an array of the UTIs we can import into an application.
	Args:		inApp
	Return:	array of NSString * UTIs
------------------------------------------------------------------------------*/

- (NSArray *)importTypesForApp:(NCApp *)inApp {
	NSDictionary * importInfo = importFormats[inApp.name];
	return importInfo ? [importInfo allKeys] : [NSArray array];
}


/*------------------------------------------------------------------------------
	Return an array of the UTIs we can import into all applications.
	Args:		--
	Return:	array of NSString * UTIs
------------------------------------------------------------------------------*/

- (NSArray *)importFileTypes {
	NSMutableSet * ftypes = [NSMutableSet setWithCapacity:20];
	for (NSString * key in importFormats) {
		[ftypes addObjectsFromArray:[importFormats[key] allKeys]];
	}
	return [ftypes allObjects];
}


/*------------------------------------------------------------------------------
	Return an array of applications that can import the given file type.
	Args:		inFileType		a UTI
	Return:	an array of app names; typically only one
				nil => we don’t know how to import that file type
------------------------------------------------------------------------------*/

- (NSArray *)applicationNamesForImport:(NSString *)inFileType {
	NSMutableArray * appNames = [NSMutableArray arrayWithCapacity:3];
	for (NSString * key in importFormats) {
		NSDictionary * importInfo = importFormats[key];
		if ([importInfo objectForKey:inFileType])
			[appNames addObject:key];
	}
	return [appNames sortedArrayUsingSelector: @selector(caseInsensitiveCompare:)];
}


/*------------------------------------------------------------------------------
	Translate desktop file to soup entries.
	Args:		--
	Return:	--
------------------------------------------------------------------------------*/

- (void)beginImport:(NCApp *)inApp context:(NCDocument *)inDocument source:(NSURL *)inURL {
	theURL = inURL;
	theContext = inDocument;
	theSoupName = nil;

	// use URL extension to find translator
	NSString * utiType = (NSString *) CFBridgingRelease(UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, (__bridge CFStringRef)[inURL pathExtension], NULL));
	NSDictionary * importInfo;

	importContext = nil;
	importTranslator = nil;
	if (utiType != nil
	&& (importInfo = [importFormats objectForKey: inApp.name]) != nil
	&& (importContext = [importInfo objectForKey: utiType]) != nil)
		importTranslator = [importContext objectForKey: @"importer"];

	if (importTranslator) {
		[importTranslator beginImport: inURL context: inDocument];
	}
	// update document progress w/ "Importing %@", [inURL lastPathComponent]
}


- (void)setImportSoupName:(NSString *)inSoupName {
	theSoupName = inSoupName;
}


- (NSString *)importSoupName {
	if (theSoupName) {
		return theSoupName;
	}
	if (importContext) {
		return [importContext objectForKey:@"soup"];
	}
	return nil;
}


- (Ref)import {
	return importTranslator ? [importTranslator import] : NILREF;
}


- (void)endImport {
	if (importTranslator) {
		[importTranslator importDone];
	}
	// cancel document progress

	theURL = nil;
	theContext = nil;
}

@end
