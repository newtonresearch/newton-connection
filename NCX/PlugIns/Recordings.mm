/*
	File:		Recordings.mm

	Contains:   Newton Notes (recordings) export functions.

	Written by: Newton Research Group, 2007.
*/

#import "NCXTranslator.h"
#import "PlugInUtilities.h"


/*------------------------------------------------------------------------------
	N e w t o n   S o u n d C o d e c   C o n s t a n t s
------------------------------------------------------------------------------*/

// data types
enum
{
	k8Bit = 8,
	k16Bit = 16
};

// compression types
enum
{
	kSampleStandard,		// uncompressed 8-bit samples
	kSampleMuLaw,			// µ-law compressed 8-bit samples
	kSampleLinear = 6		// uncompressed 16-bit samples
};


/*------------------------------------------------------------------------------
	A I F C   F o r m a t
------------------------------------------------------------------------------*/

struct AIFCHeader
{
	ContainerChunk			FORM;
	FormatVersionChunk	FVER;
	ExtCommonChunk			COMM;
	char						compressionName[16];
	SoundDataChunk			SSND;
}__attribute__((packed));


/*------------------------------------------------------------------------------
	D a t a
------------------------------------------------------------------------------*/

const AIFCHeader sndHeaderTemplate = {
	{	CANONICAL_LONG((uint32_t)FORMID), 0, CANONICAL_LONG((uint32_t)AIFCID) },
	{	CANONICAL_LONG((uint32_t)FormatVersionID), CANONICAL_LONG(4), CANONICAL_LONG((uint32_t)AIFCVersion1) },
	{	CANONICAL_LONG((uint32_t)CommonID), CANONICAL_LONG(40), CANONICAL_SHORT(1), 0, CANONICAL_SHORT(16), {0},
		CANONICAL_LONG((uint32_t)NoneType), 0 }, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	{	CANONICAL_LONG((uint32_t)SoundDataID), 0, 0, 0 }
};


/*------------------------------------------------------------------------------
	N o t e T o A u d i o
------------------------------------------------------------------------------*/

@interface NoteToAudio : NCXTranslator
{ }
@end


@implementation NoteToAudio

/*------------------------------------------------------------------------------
	Convert Notes recording soup entry to AIFF.
	Args:		inEntry			Notes soup entry
	Return:	--					
------------------------------------------------------------------------------*/

- (NSString *) export: (RefArg) inEntry
{
	// sound samples are in inEntry.|SoundPaper:NSG|.soundData.sounds[0].samples
	RefVar sound(GetFrameSlot(inEntry, MakeSymbol("SoundPaper:NSG")));
	RefVar samples;
	RefVar item;
	XTRY
	{
		XFAIL(ISNIL(sound))
		XFAIL(ISNIL(sound = GetFrameSlot(sound, MakeSymbol("soundData"))))
		XFAIL(ISNIL(sound = GetFrameSlot(sound, MakeSymbol("sounds"))))
		XFAIL(ISNIL(sound = GetArraySlot(sound, 0)))
		XFAIL(ISNIL(samples = GetFrameSlot(sound, SYMA(samples))))

		AIFCHeader snd = sndHeaderTemplate;
		unsigned int chunkLen, samplesLen = Length(samples);
		int compressionType, dataType;
		double sampleRate;

		// fill in chunk data
		// form
		chunkLen = 80 + samplesLen;
		snd.FORM.ckSize = CANONICAL_LONG(chunkLen);

		// common encoding information
		item = GetFrameSlot(sound, SYMA(compressionType));
		compressionType = NOTNIL(item) ? (int)RINT(item) : kSampleStandard;

		item = GetFrameSlot(sound, SYMA(dataType));
		if (NOTNIL(item))
		{
			dataType = (int)RINT(item);
			if (dataType == 8 || dataType == 1 || dataType == 0)	// NPG2.1 7-30 says older NTK generates dataType = 0
				dataType = k8Bit;
			else if (dataType == 16 || dataType == 2)
				dataType = k16Bit;
			else
				dataType = k8Bit;	// XFAIL(1)?
		}
		else
			dataType = (compressionType == kSampleStandard) ? k8Bit : k16Bit;

		item = GetFrameSlot(sound, SYMA(samplingRate));				// can be int, Fixed or double
		if (IsReal(item))
			sampleRate = CDouble(item);
		else if (ISINT(item))
			sampleRate = RINT(item);
		else if (IsBinary(item))
			sampleRate = *(Fixed *)BinaryData(item) / fixed1;
		else
			sampleRate = 22026.43;

		snd.COMM.numSampleFrames = CANONICAL_LONG(samplesLen);
		snd.COMM.sampleSize = CANONICAL_SHORT(dataType);
		dtox80(&sampleRate, &snd.COMM.sampleRate);	// no need to byte-swap this

		item = GetFrameSlot(sound, SYMA(codecName));
		if (IsString(item))
		{
		//	sound is compressed using a codec
			ConvertFromUnicode(GetUString(item), snd.COMM.compressionName+1, 15);
			snd.COMM.compressionName[0] = strlen(snd.COMM.compressionName+1);

			if (strcmp(snd.COMM.compressionName+1, "TMuLawCodec") == 0)
				snd.COMM.compressionType = CANONICAL_LONG((uint32_t)'ulaw');
			else if (strcmp(snd.COMM.compressionName+1, "TIMACodec") == 0)
				snd.COMM.compressionType = CANONICAL_LONG((uint32_t)'ima4');
			else if (strcmp(snd.COMM.compressionName+1, "TGSMCodec") == 0)
				snd.COMM.compressionType = CANONICAL_LONG((uint32_t)'GSM ');
		}

		// samples
		chunkLen = 8 + samplesLen;
		snd.SSND.ckSize = CANONICAL_LONG(chunkLen);

		// write header + samples to file
		NSMutableData * aiffData = [NSMutableData dataWithCapacity: sizeof(snd) + samplesLen];
		[aiffData appendBytes: &snd length: sizeof(snd)];
		[aiffData appendBytes: BinaryData(samples) length: samplesLen];

		return [self write:aiffData toFile:[self makeFilename:inEntry] extension:@"aif"];
	}
	XENDTRY;
	return nil;
}

@end

