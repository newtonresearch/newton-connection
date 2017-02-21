/*
	File:		MP3.mm

	Contains:   MP3 import/export functions.

	Written by: Newton Research Group, 2008.
*/

#import "NCXTranslator.h"
#import "PlugInUtilities.h"
#import <AudioToolbox/AudioToolbox.h>

extern Ref			GetStores(void);
extern "C" Ref		FLBAllocCompressed(RefArg inStoreWrapper, RefArg inClass, RefArg inLength, RefArg inCompanderName, RefArg inCompanderParms);


/*------------------------------------------------------------------------------
	A u d i o T o M P 3
------------------------------------------------------------------------------*/

@interface AudioToMP3 : NCXTranslator
{ }
@end


@implementation AudioToMP3

/*------------------------------------------------------------------------------
	Convert Audio soup entry of class 'MP3Data to MP3.
	Args:		inEntry			Audio soup entry
	Return:	--

	Need to add this to AudioPlugIn-Info.plist:
	<key>NCExportSoups</key>
	<array>
		<string>Audio</string>
		<dict>
			<key>mp3data</key>
			<string>AudioToMP3</string>
		</dict>
	</array>
	<key>NCExportFormat</key>
	<string>mp3</string>

------------------------------------------------------------------------------*/

- (NSString *) export: (RefArg) inEntry
{
	NSString * filename = nil;
	newton_try
	{
		NSData * mp3Data = nil;

	//	we don’t export MP3
	//	[self translateMP3: inEntry];

		if (mp3Data != nil) {
			filename = [self write:mp3Data toFile:[self makeFilename:inEntry] extension:@"mp3"];
			mp3Data = nil;
		}
	}
	newton_catch_all
	{}
	end_try;
	return filename;
}

@end



/*------------------------------------------------------------------------------
	A u d i o F r o m M P 3
------------------------------------------------------------------------------*/

@interface AudioFromMP3 : NCXTranslator
{
	BOOL isDone;
}
@end


@implementation AudioFromMP3

- (void) beginImport: (NSURL *) inURL context: (NCDocument *) inDocument
{
	[super beginImport: inURL context: inDocument];

	isDone = NO;
}


/*------------------------------------------------------------------------------
	Translate a MP3 file to an Audio soup entry of the form:

	{
		class: 'MP3Data,
		MP3Data: {
						samples: <samples:>,
					},
		title: "",
		artist: "",
		album: ""
	}

	This method will be called repeatedly until it returns NILREF.
	Args:		--
	Return:	Audio soup entry
------------------------------------------------------------------------------*/

- (Ref) import
{
	RefVar theEntry;

	if (!isDone)
	{
		size_t fileLen;
		FILE * sourceFile = fopen(theURL.fileSystemRepresentation, "r");
		fseek(sourceFile, 0, SEEK_END);
		fileLen = ftell(sourceFile);
		fseek(sourceFile, 0, SEEK_SET);

		RefVar store;
		RefVar stores(GetStores());
		if (IsArray(stores) && Length(stores) > 0)
			store = GetArraySlot(stores, 0);
		RefVar samples(FLBAllocCompressed(store, SYMA(samples), MAKEINT(fileLen), RA(NILREF), RA(NILREF)));
		fread(BinaryData(samples), 1, fileLen, sourceFile);
		fclose(sourceFile);

		RefVar theData(AllocateFrame());
		SetFrameSlot(theData, SYMA(samples), samples);

		// read metadata
		NSDictionary * info = [self id3TagsForURL:theURL];
		// only works in the main thread?
		NSLog(@"MP3 attributes: %@\n", info);

		theEntry = AllocateFrame();
		SetFrameSlot(theEntry, SYMA(class), MakeSymbol("MP3Data"));
		SetFrameSlot(theEntry, MakeSymbol("MP3Data"), theData);
		NSString * title = [info objectForKey:@kAFInfoDictionary_Title];
		if (title.length == 0)
			title = [[theURL lastPathComponent] stringByDeletingPathExtension];
		SetFrameSlot(theEntry, SYMA(title), MakeString(title));
		SetFrameSlot(theEntry, MakeSymbol("artist"), MakeString([info objectForKey:@kAFInfoDictionary_Artist]));
		SetFrameSlot(theEntry, MakeSymbol("album"), MakeString([info objectForKey:@kAFInfoDictionary_Album]));

		isDone = YES;
	}

	return theEntry;
}


- (NSDictionary *)id3TagsForURL:(NSURL *)resourceUrl
{
	NSDictionary * tagsDictionary = nil;
	AudioFileID fileID = NULL;

	XTRY
	{
		OSStatus result;
		result = AudioFileOpenURL((__bridge CFURLRef)resourceUrl, kAudioFileReadPermission, 0, &fileID);
		XFAIL(result != noErr)

		CFDictionaryRef piDict = nil;
		UInt32 piDataSize = sizeof(piDict);
		result = AudioFileGetProperty(fileID, kAudioFilePropertyInfoDictionary, &piDataSize, &piDict);
		XFAIL(result != noErr)

		tagsDictionary = [NSDictionary dictionaryWithDictionary:(__bridge NSDictionary*)piDict];
		CFRelease(piDict);
	}
	XENDTRY;
	if (fileID) {
		AudioFileClose(fileID);
	}

	return tagsDictionary;
}

@end

