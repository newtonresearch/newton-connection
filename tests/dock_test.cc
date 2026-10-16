/*
 File: dock_test.cc

 Unit tests of NCX's dock (PORTING.md Phase 5): the protocol extensions,
 the dock controller and its operations against the fake Newton
 (ncx-fake-newton, a process of its own). One section per ctest:

   ncx_dock_test <section> <ncx-fake-newton>   extensions, bits, controller,
                                               screenshot, onepiece, notoolkit,
                                               or all

 Prints "ok <what>" or "FAIL <what>" per check; exit 0 if all are ok.
 */

#include "FakeNewton.h"
#include "FakeProcess.h"

#include "base/Core.h"
#include "base/NewtonCall.h"
#include "comms/DockRefs.h"
#include "dock/DockController.h"
#include "dock/Extensions.h"
#include "dock/Screenshot.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace ncx;

namespace {

int gFailed = 0;
std::string gFakeNewton;

void Check(bool inOk, const std::string & inWhat)
{
  std::printf("%s %s\n", inOk ? "ok     " : "FAIL   ", inWhat.c_str());
  if (!inOk)
    ++gFailed;
}

/** A port of its own for each section (below 40000: Windows). */
int Port(int inSection)
{
  static int base = 21000 + (int)(std::chrono::steady_clock::now().time_since_epoch().count() % 500) * 10;
  return base + inSection;
}

/** Dock with a fake Newton (inOptions: its command line), run inWork on
    the dock thread, disconnect. The controller's result; the fake's
    report in ioFake. */
template <typename Work>
int DockWithFake(int inPort, const std::string & inOptions, FakeProcess & ioFake, Work inWork)
{
  ioFake.start(gFakeNewton, inPort, "--password test " + inOptions);
  DockController dock;
  DockController::Events events;
  events.docked = [&](const DockedNewton &) {
    dock.post([&](DockController & ioDock) { inWork(ioDock); });
    dock.disconnect();
  };
  dock.setEvents(events);
  DockController::Settings settings;
  settings.targets = { "tcp:" + std::to_string(inPort) };
  settings.password = "test";
  settings.once = true;
  settings.waitSeconds = 20;
  std::string error;
  if (!dock.start(settings, error)) {
    std::printf("        can't listen: %s\n", error.c_str());
    ioFake.finish();
    return -1;
  }
  dock.wait();
  ioFake.finish();
  if (dock.result() != 0)
    std::printf("        %s (%d)\n", dock.why().c_str(), dock.result());
  return dock.result();
}

bool Received(const FakeProcess & inFake, const char * inCommand)
{
  for (const std::string & command : inFake.received)
    if (command == inCommand)
      return true;
  return false;
}

/*-- extensions --------------------------------------------------------------*/

void TestExtensions()
{
  for (const char * id : { "NCss", "scrn" }) {
    std::vector<uint8_t> extension = Extension(Cmd(*(const char (*)[5])id));
    bool ok = extension.size() > 40 && std::memcmp(extension.data(), id, 4) == 0 && extension[4] == 2;  // NSOF 2
    if (ok) {
      // the function: a frame of class 'CodeBlock
      std::vector<uint8_t> nsof(extension.begin() + 4, extension.end());
      bool function = false;
      CallNewton([&] {
        RefVar code(UnflattenBytes(nsof));
        function = IsFrame(code) && NOTNIL(GetFrameSlot(code, MakeSymbol("instructions")));
      });
      ok = function;
    }
    Check(ok, std::string("extensions: ") + id + " is built in, a function");
  }
  Check(Extension(Cmd("none")).empty(), "extensions: no such extension");
}

/*-- bits --------------------------------------------------------------------*/

void TestBits()
{
  const uint8_t one[] = { 0xA5 };                         // 1 0 1 0 0 1 0 1
  std::vector<uint8_t> gray = GrayFromBits(one, 1, 8, 1, 1, 1);
  Check(gray == std::vector<uint8_t>({ 0, 255, 0, 255, 255, 0, 255, 0 }), "bits: 1 bit a pixel");
  const uint8_t two[] = { 0x1B };                         // 0 1 2 3
  gray = GrayFromBits(two, 1, 4, 1, 1, 2);
  Check(gray == std::vector<uint8_t>({ 255, 170, 85, 0 }), "bits: 2 bits a pixel");
  const uint8_t four[] = { 0x0F, 0x80, 0xFF, 0xFF };      // rows of 2 bytes, 3 pixels used
  gray = GrayFromBits(four, 4, 3, 2, 2, 4);
  Check(gray == std::vector<uint8_t>({ 255, 0, 119, 0, 0, 0 }), "bits: 4 bits a pixel, rows with padding");
  const uint8_t eight[] = { 0, 128, 255 };
  gray = GrayFromBits(eight, 3, 3, 1, 3, 8);
  Check(gray == std::vector<uint8_t>({ 255, 127, 0 }), "bits: 8 bits a pixel");
  gray = GrayFromBits(four, 2, 3, 2, 2, 4);
  Check(gray.size() == 6 && gray[3] == 255, "bits: too few bytes stay white");
}

/*-- controller --------------------------------------------------------------*/

void TestController()
{
  FakeProcess fake;
  int stores = -1;
  std::string name;
  int result = DockWithFake(Port(1), "", fake, [&](DockController & dock) {
    name = dock.newton().name;
    dock.setDesktopControl(true);
    RefVar all(dock.session().getAllStores());
    stores = IsArray(all) ? (int)Length(all) : -1;
    dock.setDesktopControl(false);
  });
  Check(result == 0 && fake.exitCode == 0, "controller: docked and disconnected " + fake.why);
  Check(name == "Ada Lovelace" && stores == 1, "controller: an operation on the dock thread");
  Check(Received(fake, "dsnc") && Received(fake, "gsto") && Received(fake, "opdn") && !fake.received.empty()
        && fake.received.back() == "disc", "controller: in control, the stores, done, disc");

  // a Newton exception in an operation fails it, not the session
  FakeProcess fake2;
  bool after = false;
  result = DockWithFake(Port(2), "", fake2, [&](DockController & dock) {
    try {
      CallNewton([] { GetFrameSlot(RefVar(MAKEINT(1)), MakeSymbol("x")); });
    } catch (const NewtonException &) {
      after = true;
    }
  });
  Check(result == 0 && after && fake2.exitCode == 0, "controller: a Newton exception is caught");

  // stop() while waiting for a Newton
  DockController dock;
  DockController::Settings settings;
  settings.targets = { "tcp:" + std::to_string(Port(3)) };
  std::string error;
  bool started = dock.start(settings, error);
  auto before = std::chrono::steady_clock::now();
  dock.stop();
  double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count();
  Check(started && seconds < 2 && !dock.isDocked(), "controller: stop() while listening");
}

/*-- screenshot --------------------------------------------------------------*/

void TestScreenshot()
{
  FakeProcess fake;
  Screenshot shot;
  int began = -1, took = -1;
  Progress progress;
  int result = DockWithFake(Port(4), "", fake, [&](DockController & dock) {
    ScreenshotSession screenshots(dock);
    began = screenshots.begin();
    if (began == 0)
      took = screenshots.take(shot, &progress);
    screenshots.end();
  });
  Check(result == 0 && began == 0 && took == 0, "screenshot: taken " + fake.why);
  Check(shot.width == 320 && shot.height == 480 && shot.depth == 4 && shot.gray.size() == 320 * 480,
        "screenshot: 320 x 480, 4 bits a pixel");
  bool pixels = shot.gray.size() == 320 * 480;
  for (int y = 0; pixels && y < 480; ++y)
    for (int x = 0; pixels && x < 320; ++x)
      pixels = shot.gray[(size_t)y * 320 + x] == 255 - ScreenPixel(x, y) * 17;
  Check(pixels, "screenshot: every pixel");
  Check(fake.largestBlock > 0 && fake.largestBlock <= (size_t)ScreenshotSession::kBlock,
        "screenshot: rows of at most 2048 bytes at a time (" + std::to_string(fake.largestBlock) + ")");
  Check(progress.total() == 76800 && progress.completed() == 76800, "screenshot: the progress");
  Check(fake.blankBlocks == 5, "screenshot: white rows not sent (" + std::to_string(fake.blankBlocks) + " of 40)");
  Check(fake.extensions == std::vector<std::string>({ "scrn", "NCss" }), "screenshot: the extensions sent");
}

void TestOnePiece()
{
  FakeProcess fake;
  Screenshot shot;
  int took = -1;
  int result = DockWithFake(Port(6), "", fake, [&](DockController & dock) {
    ScreenshotSession screenshots(dock);
    screenshots.setBlock(0);
    if (screenshots.begin() == 0)
      took = screenshots.take(shot);
    screenshots.end();
  });
  bool pixels = result == 0 && took == 0 && shot.gray.size() == 320 * 480;
  for (int y = 0; pixels && y < 480; ++y)
    for (int x = 0; pixels && x < 320; ++x)
      pixels = shot.gray[(size_t)y * 320 + x] == 255 - ScreenPixel(x, y) * 17;
  Check(pixels && fake.largestBlock == 0, "onepiece: the bits with the shot, no reads");
}

void TestNoToolkit()
{
  FakeProcess fake;
  int began = 0;
  int result = DockWithFake(Port(5), "--no-toolkit", fake, [&](DockController & dock) {
    ScreenshotSession screenshots(dock);
    began = screenshots.begin();
  });
  Check(result == 0 && began == kDockErrFileNotFound, "notoolkit: no Toolkit app, no screenshot");
  Check(Received(fake, "opdn"), "notoolkit: the desktop gave up control");
}

} // namespace


int main(int argc, char * argv[])
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::string section = argc > 1 ? argv[1] : "all";
  gFakeNewton = argc > 2 ? argv[2] : "ncx-fake-newton";
  InitCore();
  struct { const char * name; void (*test)(); } sections[] = {
    { "extensions", TestExtensions }, { "bits", TestBits }, { "controller", TestController },
    { "screenshot", TestScreenshot }, { "onepiece", TestOnePiece }, { "notoolkit", TestNoToolkit } };
  bool found = false;
  for (auto & s : sections)
    if (section == "all" || section == s.name) {
      s.test();
      found = true;
    }
  if (!found) {
    std::fprintf(stderr, "no section \"%s\"\n", section.c_str());
    return 2;
  }
  std::printf("%s\n", gFailed ? "FAILED" : "all ok");
  return gFailed ? 1 : 0;
}
