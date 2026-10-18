/*
 File: Audio.cc

 Sound (PORTING.md 6.5; the Cocoa app's NoteToAudio and AudioFromMP3):
   - a recording (Notes' '|soundPaper:NSG|: soundData.sounds, each
     {samples, samplingRate, dataType, compressionType, codecName}) as a
     WAV file: 8-bit samples as they are (unsigned, 0x80 silence, as the
     Mac's), 16-bit ones turned little-endian, mu-law decoded to 16 bits;
     samples of another codec (IMA, GSM) as AIFF-C naming it, as the Cocoa
     app wrote all of them
   - an MP3 file as an entry of the Newton's MAD Max player ('MP3Data:
     its bytes, which the player plays, and the title, artist and album of
     its ID3 tags), and such an entry back as the MP3 file
 */

#include "translators/Translator.h"
#include "base/NewtonStrings.h"
#include "base/Strings.h"
#include "platform/AppFolders.h"

#include "RSSymbols.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>

namespace ncx {

namespace {

enum { kSampleStandard = 0, kSampleMuLaw = 1, kSampleLinear = 6 };

void PutLE(std::string & ioData, uint32_t inValue, int inBytes)
{
  for (int i = 0; i < inBytes; ++i)
    ioData += (char)(inValue >> (8 * i));
}

void PutBE(std::string & ioData, uint32_t inValue, int inBytes)
{
  for (int i = inBytes - 1; i >= 0; --i)
    ioData += (char)(inValue >> (8 * i));
}

/** mu-law (G.711) to a 16-bit sample. */
int16_t MuLaw(uint8_t inByte)
{
  inByte = ~inByte;
  int sign = inByte & 0x80, exponent = (inByte >> 4) & 7, mantissa = inByte & 15;
  int sample = (((mantissa << 3) + 0x84) << exponent) - 0x84;
  return (int16_t)(sign ? -sample : sample);
}

/** 80-bit IEEE extended, big-endian (AIFF's sample rate). */
void PutExtended(std::string & ioData, double inValue)
{
  int exponent = 0;
  double mantissa = std::frexp(inValue, &exponent);      // 0.5 <= mantissa < 1
  uint64_t bits = inValue > 0 ? (uint64_t)std::ldexp(mantissa, 64) : 0;
  uint16_t head = inValue > 0 ? (uint16_t)(exponent - 1 + 16383) : 0;
  PutBE(ioData, head, 2);
  PutBE(ioData, (uint32_t)(bits >> 32), 4);
  PutBE(ioData, (uint32_t)bits, 4);
}

struct Sound
{
  std::string samples;
  double rate = 22026.43;
  int bits = 8;
  int compression = kSampleStandard;
  std::string codec;
};

Sound SoundOf(RefArg inSound)
{
  Sound sound;
  RefVar samples(GetFrameSlot(inSound, SYMA(samples)));
  if (IsBinary(samples))
    sound.samples.assign((const char *)BinaryData(samples), Length(samples));
  RefVar item(GetFrameSlot(inSound, SYMA(compressionType)));
  sound.compression = ISINT(item) ? (int)RINT(item) : kSampleStandard;
  item = GetFrameSlot(inSound, SYMA(dataType));
  if (ISINT(item))
    sound.bits = RINT(item) == 16 || RINT(item) == 2 ? 16 : 8;        // (older NTK: 0, 1; NPG 2.1 7-30)
  else
    sound.bits = sound.compression == kSampleLinear ? 16 : 8;
  item = GetFrameSlot(inSound, SYMA(samplingRate));                // an integer, a real, or a Fixed
  if (IsReal(item))
    sound.rate = CDouble(item);
  else if (ISINT(item))
    sound.rate = (double)RINT(item);
  else if (IsBinary(item) && Length(item) >= 4) {
    const uint8_t * p = (const uint8_t *)BinaryData(item);
    sound.rate = (double)((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]) / 65536.0;
  }
  item = GetFrameSlot(inSound, SYMA(codecName));
  if (IsString(item))
    sound.codec = UTF8FromString(item);
  return sound;
}

std::string WAV(const std::string & inPCM, int inBits, uint32_t inRate)
{
  std::string wav = "RIFF";
  PutLE(wav, 36 + (uint32_t)inPCM.size(), 4);
  wav += "WAVEfmt ";
  PutLE(wav, 16, 4);
  PutLE(wav, 1, 2);                                  // PCM
  PutLE(wav, 1, 2);                                  // mono
  PutLE(wav, inRate, 4);
  PutLE(wav, inRate * (inBits / 8), 4);
  PutLE(wav, inBits / 8, 2);
  PutLE(wav, inBits, 2);
  wav += "data";
  PutLE(wav, (uint32_t)inPCM.size(), 4);
  wav += inPCM;
  if (inPCM.size() & 1)
    wav += '\0';
  return wav;
}

/** AIFF-C of a codec's samples (the Cocoa app's header). */
std::string AIFC(const Sound & inSound)
{
  std::string type = "NONE";
  if (inSound.codec == "TMuLawCodec")
    type = "ulaw";
  else if (inSound.codec == "TIMACodec")
    type = "ima4";
  else if (inSound.codec == "TGSMCodec")
    type = "GSM ";
  std::string name = inSound.codec.substr(0, 15);
  std::string comm;
  PutBE(comm, 1, 2);                                 // channels
  PutBE(comm, (uint32_t)inSound.samples.size(), 4);  // frames (as the Cocoa app counted them)
  PutBE(comm, (uint32_t)inSound.bits, 2);
  PutExtended(comm, inSound.rate);
  comm += type;
  comm += (char)name.size();
  comm += name;
  if ((name.size() + 1) & 1)
    comm += '\0';
  std::string body = "AIFC";
  body += "FVER";
  PutBE(body, 4, 4);
  PutBE(body, 0xA2805140, 4);                        // AIFC version 1
  body += "COMM";
  PutBE(body, (uint32_t)comm.size(), 4);
  body += comm;
  body += "SSND";
  PutBE(body, 8 + (uint32_t)inSound.samples.size(), 4);
  PutBE(body, 0, 4);
  PutBE(body, 0, 4);
  body += inSound.samples;
  if (inSound.samples.size() & 1)
    body += '\0';
  std::string aifc = "FORM";
  PutBE(aifc, (uint32_t)body.size(), 4);
  return aifc + body;
}


class NoteToAudio : public Exporter
{
public:
  std::filesystem::path exportEntry(RefArg inEntry) override
  {
    // inEntry.|soundPaper:NSG|.soundData.sounds: one after the other if they are alike
    RefVar paper(GetFrameSlot(inEntry, MakeSymbol("SoundPaper:NSG")));
    RefVar data(IsFrame(paper) ? GetFrameSlot(paper, MakeSymbol("soundData")) : (Ref)NILREF);
    RefVar sounds(IsFrame(data) ? GetFrameSlot(data, MakeSymbol("sounds")) : (Ref)NILREF);
    if (!IsArray(sounds) || Length(sounds) == 0)
      return std::filesystem::path();
    Sound sound = SoundOf(RefVar(GetArraySlot(sounds, 0)));
    for (ArrayIndex i = 1; i < Length(sounds); ++i) {
      Sound more = SoundOf(RefVar(GetArraySlot(sounds, i)));
      if (more.bits == sound.bits && more.compression == sound.compression && more.codec == sound.codec)
        sound.samples += more.samples;
    }
    if (sound.samples.empty())
      return std::filesystem::path();
    std::string name = EntryFileName(inEntry);
    uint32_t rate = (uint32_t)(sound.rate + 0.5);
    bool muLaw = sound.compression == kSampleMuLaw || sound.codec == "TMuLawCodec";
    if (muLaw) {
      std::string pcm;
      for (unsigned char b : sound.samples)
        PutLE(pcm, (uint16_t)MuLaw(b), 2);
      return write(WAV(pcm, 16, rate), name, "wav");
    }
    if (sound.codec.empty() && sound.bits == 16) {
      std::string pcm = sound.samples;               // big-endian on the Newton
      for (size_t i = 0; i + 1 < pcm.size(); i += 2)
        std::swap(pcm[i], pcm[i + 1]);
      return write(WAV(pcm, 16, rate), name, "wav");
    }
    if (sound.codec.empty())
      return write(WAV(sound.samples, 8, rate), name, "wav");
    return write(AIFC(sound), name, "aifc");
  }
};


class MP3Exporter : public Exporter
{
public:
  std::filesystem::path exportEntry(RefArg inEntry) override
  {
    RefVar data(GetFrameSlot(inEntry, MakeSymbol("MP3Data")));
    RefVar samples(IsFrame(data) ? GetFrameSlot(data, SYMA(samples)) : (Ref)NILREF);
    if (!IsBinary(samples))
      return std::filesystem::path();
    return write(std::string((const char *)BinaryData(samples), Length(samples)), EntryFileName(inEntry), "mp3");
  }
};


/*-- ID3 ---------------------------------------------------------------------*/

/** An ID3v2 text frame's text: an encoding byte (0 Latin-1, 1 UTF-16 with a
    BOM, 2 UTF-16BE, 3 UTF-8), the text. */
std::string ID3Text(const std::string & inFrame)
{
  if (inFrame.empty())
    return std::string();
  unsigned char encoding = (unsigned char)inFrame[0];
  std::string data = inFrame.substr(1);
  std::string text;
  if (encoding == 1 || encoding == 2) {
    bool big = encoding == 2;
    size_t i = 0;
    if (encoding == 1 && data.size() >= 2) {
      big = (unsigned char)data[0] == 0xFE;
      i = 2;
    }
    std::u16string chars;
    for (; i + 1 < data.size(); i += 2) {
      char16_t c = big ? (char16_t)((unsigned char)data[i] << 8 | (unsigned char)data[i + 1])
                       : (char16_t)((unsigned char)data[i + 1] << 8 | (unsigned char)data[i]);
      if (c == 0)
        break;
      chars += c;
    }
    text = UTF8FromUTF16(chars);
  } else if (encoding == 3) {
    text = data.substr(0, data.find('\0'));
  } else {
    for (unsigned char c : data) {
      if (c == 0)
        break;
      if (c < 0x80)
        text += (char)c;
      else {
        text += (char)(0xC0 | c >> 6);
        text += (char)(0x80 | (c & 0x3F));
      }
    }
  }
  while (!text.empty() && text.back() == ' ')
    text.pop_back();
  return text;
}

struct Tags { std::string title, artist, album; };

/** An MP3 file's ID3 tags: v2 (2.2, 2.3, 2.4) at its start, else v1 at
    its end. */
Tags ID3Tags(const std::string & inFile)
{
  Tags tags;
  if (inFile.size() >= 10 && inFile.compare(0, 3, "ID3") == 0) {
    int version = (unsigned char)inFile[3];
    auto syncsafe = [&](size_t at) {
      return (uint32_t)((unsigned char)inFile[at] & 0x7F) << 21 | (uint32_t)((unsigned char)inFile[at + 1] & 0x7F) << 14
             | (uint32_t)((unsigned char)inFile[at + 2] & 0x7F) << 7 | ((unsigned char)inFile[at + 3] & 0x7F);
    };
    size_t end = std::min(inFile.size(), (size_t)10 + syncsafe(6));
    size_t at = 10;
    size_t header = version == 2 ? 6 : 10;
    while (at + header <= end) {
      std::string id = inFile.substr(at, version == 2 ? 3 : 4);
      if (id[0] == 0)
        break;
      uint32_t size;
      if (version == 2)
        size = (uint32_t)(unsigned char)inFile[at + 3] << 16 | (uint32_t)(unsigned char)inFile[at + 4] << 8 | (unsigned char)inFile[at + 5];
      else if (version == 4)
        size = syncsafe(at + 4);
      else
        size = (uint32_t)(unsigned char)inFile[at + 4] << 24 | (uint32_t)(unsigned char)inFile[at + 5] << 16
               | (uint32_t)(unsigned char)inFile[at + 6] << 8 | (unsigned char)inFile[at + 7];
      if (at + header + size > end)
        break;
      std::string frame = inFile.substr(at + header, size);
      if (id == "TIT2" || id == "TT2")
        tags.title = ID3Text(frame);
      else if (id == "TPE1" || id == "TP1")
        tags.artist = ID3Text(frame);
      else if (id == "TALB" || id == "TAL")
        tags.album = ID3Text(frame);
      at += header + size;
    }
  }
  if (tags.title.empty() && inFile.size() >= 128 && inFile.compare(inFile.size() - 128, 3, "TAG") == 0) {
    auto field = [&](size_t at, size_t length) { return ID3Text(std::string(1, '\0') + inFile.substr(inFile.size() - 128 + at, length)); };
    tags.title = field(3, 30);
    tags.artist = field(33, 30);
    tags.album = field(63, 30);
  }
  return tags;
}


class AudioFromMP3 : public Importer
{
public:
  bool begin(const std::filesystem::path & inFile, const TranslatorContext & inContext) override
  {
    Importer::begin(inFile, inContext);
    std::ifstream in(inFile, std::ios::binary);
    if (!in) {
      mWhy = "can't read the file";
      return false;
    }
    mData.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
  }

  Ref next(std::string &) override
  {
    if (mDone)
      return NILREF;
    mDone = true;
    Tags tags = ID3Tags(mData);
    RefVar samples(AllocateBinary(SYMA(samples), mData.size()));
    std::memcpy(BinaryData(samples), mData.data(), mData.size());
    RefVar data(AllocateFrame());
    SetFrameSlot(data, SYMA(samples), samples);
    RefVar entry(AllocateFrame());
    SetFrameSlot(entry, SYMA(class), MakeSymbol("MP3Data"));
    SetFrameSlot(entry, MakeSymbol("MP3Data"), data);
    SetFrameSlot(entry, SYMA(title), RefVar(MakeStringFromUTF8(tags.title.empty() ? UTF8FromPath(mFile.stem()) : tags.title)));
    SetFrameSlot(entry, MakeSymbol("artist"), RefVar(MakeStringFromUTF8(tags.artist)));
    SetFrameSlot(entry, MakeSymbol("album"), RefVar(MakeStringFromUTF8(tags.album)));
    return entry;
  }

private:
  std::string mData;
  bool mDone = false;
};

} // namespace


std::unique_ptr<Exporter> MakeNoteToAudio() { return std::make_unique<NoteToAudio>(); }
std::unique_ptr<Exporter> MakeMP3Exporter() { return std::make_unique<MP3Exporter>(); }
std::unique_ptr<Importer> MakeAudioFromMP3() { return std::make_unique<AudioFromMP3>(); }

} // namespace ncx
