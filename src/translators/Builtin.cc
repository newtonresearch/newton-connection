/*
 File: Builtin.cc

 The translators every entry can fall back on (the Cocoa app's
 NCDefaultTranslator, NCTextTranslator, NCPackageTranslator):
   - the default one prints the entries as NewtonScript, all of an app's
     into "Newton <app>.text" (PrintObject, depth 16)
   - text: an entry with a text slot as "<title>.text"
   - a package: its pkgRef as "<packageName>.newtonpkg"
 - an image (a NewtPaint picture: an image slot with a bitmap) as
   "<title>.png" (the Cocoa app wrote TIFF)
 */

#include "translators/Translator.h"
#include "translators/Render.h"
#include "base/NewtonStrings.h"

#include "Globals.h"
#include "RSSymbols.h"

#include <cstdio>
#include <memory>

extern "C" void PrintObject(Ref inObj, int indent);
void RedirectStdioOutTranslator(FILE * inFRef);

namespace ncx {

namespace {

class DefaultExporter : public Exporter
{
public:
  ~DefaultExporter() override { finish(); }

  void begin(const TranslatorContext & inContext) override
  {
    Exporter::begin(inContext);
    mFile = write(std::string(), "Newton " + inContext.appName, "text");
    if (mFile.empty())
      return;
#if defined(_WIN32)
    mOut = _wfopen(mFile.wstring().c_str(), L"w");
#else
    mOut = std::fopen(mFile.c_str(), "w");
#endif
    if (!mOut)
      return;
    RedirectStdioOutTranslator(mOut);
    mSavedDepth = GetFrameSlot(RA(gVarFrame), SYMA(printDepth));
    mSavedLength = GetFrameSlot(RA(gVarFrame), SYMA(printLength));
    SetFrameSlot(RA(gVarFrame), SYMA(printDepth), MAKEINT(16));
    SetFrameSlot(RA(gVarFrame), SYMA(printLength), RA(NILREF));
  }

  std::filesystem::path exportEntry(RefArg inEntry) override
  {
    if (!mOut)
      return std::filesystem::path();
    newton_try
    {
      PrintObject(inEntry, 0);
    }
    newton_catch_all
    {
      std::fprintf(mOut, "\n*** Error printing object (%ld). ***\n", (long)(intptr_t)CurrentException()->data);
    }
    end_try;
    std::fputs("\n\n", mOut);
    return std::filesystem::path();
  }

  std::filesystem::path end() override
  {
    finish();
    return mFile;
  }

private:
  void finish()
  {
    if (!mOut)
      return;
    SetFrameSlot(RA(gVarFrame), SYMA(printDepth), mSavedDepth);
    SetFrameSlot(RA(gVarFrame), SYMA(printLength), mSavedLength);
    RedirectStdioOutTranslator(nullptr);
    std::fclose(mOut);
    mOut = nullptr;
  }

  std::filesystem::path mFile;
  FILE * mOut = nullptr;
  RefStruct mSavedDepth, mSavedLength;
};


class TextExporter : public Exporter
{
public:
  std::filesystem::path exportEntry(RefArg inEntry) override
  {
    RefVar text(GetFrameSlot(inEntry, SYMA(text)));
    if (!IsString(text))
      return std::filesystem::path();
    std::string title = StringSlot(inEntry, "title");
    return write(UTF8FromStringLF(text), title.empty() ? "Text" : title, "text");
  }
};


class PackageExporter : public Exporter
{
public:
  std::filesystem::path exportEntry(RefArg inEntry) override
  {
    RefVar package(GetFrameSlot(inEntry, MakeSymbol("pkgRef")));
    if (!IsBinary(package))
      return std::filesystem::path();
    std::string bytes((const char *)BinaryData(package), Length(package));
    std::string name = StringSlot(inEntry, "packageName");
    return write(bytes, name.empty() ? "Newton Package" : name, "newtonpkg");
  }
};


class ImageExporter : public Exporter
{
public:
  std::filesystem::path exportEntry(RefArg inEntry) override
  {
    Pixels pixels = RenderBitmap(RefVar(GetFrameSlot(inEntry, MakeSymbol("image"))));
    std::string png = PNGData(pixels);
    if (png.empty())
      return std::filesystem::path();
    std::string title = StringSlot(inEntry, "title");
    return write(png, title.empty() ? "Newton Image" : title, "png");
  }
};

} // namespace


std::unique_ptr<Exporter> MakeImageExporter() { return std::make_unique<ImageExporter>(); }
std::unique_ptr<Exporter> MakeDefaultExporter() { return std::make_unique<DefaultExporter>(); }
std::unique_ptr<Exporter> MakeTextExporter() { return std::make_unique<TextExporter>(); }
std::unique_ptr<Exporter> MakePackageExporter() { return std::make_unique<PackageExporter>(); }

} // namespace ncx
