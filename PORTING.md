# Porting NCX to Windows, Linux and macOS

Target: one C++17 code base, built with CMake, FLTK for the GUI, running on
Windows, Linux and macOS, each on x86_64 and arm64. GitHub Actions builds,
tests and releases all six.

This file has three parts: the assessment (what is there), the target
architecture, and the plan, chopped into steps that each end in something
that builds and can be checked.

---

## 1. Assessment

### 1.1 What NCX is made of

About 26,700 lines of Objective-C/Objective-C++ in `NCX/`, plus 8 storyboards,
a Core Data model, a vendored libical (plain C), resources, and the prebuilt
x86_64-only `Newton.framework`.

| Area | Lines | Files | Port effort |
|---|---:|---|---|
| Comms: endpoints, MNP, dock events, session, cursor, CRC, DES | 6,350 | `Comms/**` | medium: the logic is C-like, the plumbing is GCD/ObjC |
| Dock protocol controller and document | 3,660 | `NCDockProtocolController`, `NCDocument` | large: central, touches everything |
| Components (backup, restore, packages, ROM, screenshot, keyboard, Newton 1) | 2,800 | `*Component.mm`, `PkgPart` | medium |
| Data model (Core Data entities) | 1,490 | `NCStore`, `NCSoup`, `NCEntry`, `NCApp`, `NCDevice`, … | medium: Core Data goes away |
| Translators ("plugins": Names, Dates, Notes, Works, Audio) | 5,400 | `PlugIns/**` | medium–large: RTF, images, iCal, vCard |
| UI controllers, views, app delegate | 4,550 | `*ViewController`, `NCWindowController`, … | rewrite |
| Small utilities | 500 | `Utilities`, `NSString-Extensions`, … | small |
| **Dead code (not in any Xcode target)** | 1,990 | `SyncComponent`, `BluetoothEndpoint`, `StoreInfo` | **drop** |

Storyboards (UI definitions): `MainWindow` (toolbar, 3-pane split, source
list), `Main` (menu bar with 59 items, preferences tabs), `Device`,
`Store`, `Soup`, `Package`, `Screenshot`, `Keyboard`. They use ordinary
controls only; there is nothing exotic in them.

### 1.2 Apple APIs in use and what replaces them

| Apple API | Used for | Replacement |
|---|---|---|
| Foundation (`NSString`, `NSArray`, `NSDictionary`, `NSData`, `NSNumber`, `NSURL`, …) | everything | `std::string` (UTF-8), `std::vector`, `std::map`, `std::filesystem` |
| `NSDate`, `NSDateFormatter`, `NSCalendar` | Newton ↔ desktop dates, file names, iCal | `<chrono>`, `strftime`, small helpers |
| GCD (`dispatch_queue`, `dispatch_semaphore`, `dispatch_source`) | I/O thread, event queue, tickler, install queue | `std::thread` and a small serial-queue class; a hand-written semaphore (C++17 has none) |
| `NSNotificationCenter`, KVO, bindings | UI updates from the dock | observer/callback class; `Fl::awake()` to reach the UI thread |
| `NSProgress` | progress and cancel | a small `Progress` struct (atomic counters and a cancel flag) |
| Core Data (`NSPersistentDocument`, `NSManagedObject`, SQLite store) | one `.newtondevice` file per Newton | SQLite (amalgamation) with the **same schema** (see 1.5) |
| `NSUserDefaults` | preferences (`PreferenceKeys.h`) | `Fl_Preferences` |
| `NSBundle` plugins | translators loaded at runtime | translators linked into the app, kept in a registry |
| `NSAttributedString` RTF/RTFD | Notes and Works export | our own small RTF writer (see decision D4) |
| CoreGraphics (`CGContext`) | drawing ink, shapes, bitmaps (through Newton.framework `InitDrawing`/`DrawBitmap`/…) | the framework's QuickDraw functions on FLTK (step 0.5): they draw on FLTK's current surface, a window or an `Fl_Image_Surface` |
| `NSImage`, TIFF | screenshots, icons, exported images | `Fl_RGB_Image`, `fl_write_png`, PNG instead of TIFF |
| AudioToolbox `AudioFile*` | MP3 import: reading the file and its tags | dr_mp3 or minimp3 (single header, public domain) and a small ID3 reader |
| IOKit serial matching | listing serial ports | macOS: keep IOKit (plain C); Linux: `/dev/serial/by-id`, `ttyUSB*`, `ttyACM*`; Windows: registry `HARDWARE\DEVICEMAP\SERIALCOMM` |
| `termios` | serial port setup | keep on POSIX; Win32 `CreateFile`/`DCB`/overlapped I/O on Windows |
| `mkfifo` named pipes | Einstein endpoint | keep on POSIX; Einstein on Windows uses TCP |
| `NSNetService` (Bonjour) | advertising the TCP endpoint | optional; `dns_sd` on macOS, leave out elsewhere at first |
| IOKit power management | keep the Mac awake during a transfer | `IOPMAssertion` (macOS), `SetThreadExecutionState` (Windows), nothing on Linux at first |
| `SCDynamicStoreCopyComputerName` | desktop name sent to the Newton | `gethostname` / `GetComputerNameW` |
| SyncServices, IOBluetooth | dead code | dropped |
| QuickLookUI, Sparkle | linked only, or already removed | dropped |

### 1.3 What NCX needs from newton-framework

The built NCX binary and its plugins import about 80 framework symbols, and
they fall into a small set of areas:

- the object system: `Ref`/`RefVar`/`RefArg`, frames, arrays, symbols,
  binaries, `CObjectIterator`, `Clone`/`DeepClone`, `EQRef`, the `RSSYM…`
  ROM symbols, exceptions (`Throw`, handlers, `ex…` names), `REPprintf`;
- NSOF: `FlattenRef`, `FlattenRefSize`, `UnflattenRef`, and the pipes
  (`CPtrPipe`, `CStdIOPipe`);
- packages: `NewtonPackage`, `PackageParts.h`;
- dates and strings: `MakeDate`, `MakeString…`, `GetUString`, `FDate`;
- drawing: `InitDrawing`, `DrawBitmap`, `DrawBits`, `DrawShape`,
  `DrawPolygon`, `DrawPicture`, `InkDrawInRect`, and rectangle helpers
  (since 0.5 declared in `Graphics/DrawShape.h` and `Ink.h`; no
  `InitDrawing`: they draw on FLTK's current surface);
- compressed large binaries: `FLBAllocCompressed`, `GetStores` (for
  importing sounds and Works documents);
- Cocoa bridges (`MakeNSString`, `MakeNSDate`, `MakeNSFont`,
  `MakeNSDateComponents`, `ApplicationSupportFolder`, …). Several of
  these live in NCX already (`PlugInUtilities.mm`); all of them become
  plain C++ helpers in NCX.

The NewtonScript compiler, the interpreter, views, recognition, and tasks
are not needed. That makes the "NCX support library" you suggested a good
fit: a static library target in newton-framework with just these parts.

### 1.4 State of newton-framework (`dap_to_toolbox`)

What's in good shape:
- It builds with CMake, and `newtc` builds and runs natively on arm64
  macOS. The 64-bit Ref work is done.
- Earlier commits fixed Linux and Windows (MSVC) builds (2024/2025), and
  `CMakeSettings.json` has MSVC x64 and arm64 configurations.
- `Matt/NewtDump` has a desktop-side Dock link written in portable C++:
  `DockLink`, `MNP.cc`, `NTKTransport.cc` (TCP server, TCP client,
  serial, Einstein PTY), a small NSOF reader/writer, and DES. It also has
  a fake Newton for tests. This is the "inject code into the Dock
  protocol" example from CLAUDE.md, and it's what NCX's comms layer can
  build on.
- `Host/FLTK` already draws Newton shapes with FLTK, using
  `Fl_Image_Surface` canvases.

What needs fixing before NCX can use it:
1. **`typedef long Ref;`** (and `unsigned long` for `VAddr` and
   `OpaqueRef`) in `NewtonTypes.h`. On 64-bit Windows `long` is 32 bits but
   pointers are 64 bits, so pointer Refs don't fit. These must become
   `intptr_t`/`uintptr_t`.
2. **`__LP64__` is defined by hand** in CMake (`-D__LP64__=1`) and tested
   in 14 files. MSVC never defines it. Replace it with a framework macro
   derived from the pointer size.
3. **ROM data was in GNU assembler** (`ROMData/forNewton/*.s`, about 3 MB
   compiled into the library), which MSVC's `cl` can't assemble. Solved in
   0.1b (D1): ResMaker, now a CMake target, turned out to reproduce the
   `.s` files exactly, so it got a C back end, and the data is plain C99
   now (checked object for object against the assembler data).
4. **`QDDrawing.cc` draws through Quartz** (`CGContextRef quartz`). Linux
   and Windows need another back end. Solved in 0.5: no Quartz at all
   (MessagePad, the only other user, won't be implemented); the drawing
   goes through FLTK.
5. The CMake file assumes it is the top-level project (`CMAKE_SOURCE_DIR`,
   global ASan flags, `execute_process(ls …)`, macOS frameworks linked
   unconditionally). To be pulled in with `FetchContent` it has to work as
   a subproject.
6. `NTKTransport` is POSIX-only ("POSIX only for now"). Windows needs
   serial and Winsock implementations. `select()` on Windows works for
   sockets only, so serial ports there need a reader thread.

### 1.5 Backup files: keeping existing `.newtondevice` files

Each Newton's data lives in
`~/Library/Application Support/Newton Connection/<id>.newtondevice`, a
Core Data SQLite store. I opened one: the schema is six flat tables
(`ZDEVICE`, `ZUSERINFO`, `ZSTORE`, `ZAPP`, `ZSOUP`, `ZENTRY`) plus Core
Data's `Z_PRIMARYKEY` and `Z_METADATA`.
- Entries, soup info and soup indexes are **NSOF blobs**, which the
  framework already reads.
- `ZDEVICE.ZINFO` is the raw `NewtonInfo` struct.
- Only `ZUSERINFO.ZFOLDERS`/`ZFONT` are Apple binary plists, and the
  Newton sends that data again on every connection.

So a plain C++ SQLite layer can read existing backups, and if it maintains
`Z_PK`/`Z_ENT`/`Z_OPT`/`Z_PRIMARYKEY` it can also write files the old
macOS NCX still opens. Timestamps are seconds since 2001-01-01, as in Core
Data.

### 1.6 Widgets: what FLTK covers and what it doesn't

FLTK 1.4.5 (in `~/dev/fltk-1.4.git`) covers almost everything:

| Cocoa | Where | FLTK |
|---|---|---|
| `NSOutlineView` source list | main window sidebar | `Fl_Tree` ✓ |
| `NSTableView` and `NSArrayController` (sorting, multiple selection, columns per soup) | soup entries, package lists | `Fl_Table_Row` subclass (some work: sorting, column info, selection) |
| `NSSplitViewController` with collapsing panes | sidebar / content / inspector | `Fl_Tile`, collapsing done in code |
| `NSToolbar` | main window | a row of image buttons in an `Fl_Flex` (no "customize toolbar") |
| `NSSegmentedControl` | show/hide panes | radio/toggle `Fl_Button`s in a group |
| `NSLevelIndicator` | store capacity | `Fl_Progress` or a 20-line custom widget |
| `NSStackView`, `NSBox` | info panels | `Fl_Flex`, `Fl_Grid`, framed `Fl_Group` ✓ |
| `NSTabViewController` | preferences | `Fl_Tabs` ✓ |
| `NSPopUpButton`, `NSTextField`, `NSButton`, `NSImageView` | everywhere | `Fl_Choice`, `Fl_Input`/`Fl_Box`, `Fl_Button`, `Fl_Box` with an image ✓ |
| `NSTextView` with attributed text | inspector (entry contents) | `Fl_Text_Display` with a style table, or `Fl_Help_View` (HTML subset) |
| Main menu | 59 items | `Fl_Sys_Menu_Bar` (native on macOS) ✓ |
| Open/save panels, alerts, sheets | | `Fl_Native_File_Chooser`, `fl_choice`; sheets become modal windows ✓ |
| Dropping files and text **into** NCX | device, store, soup, keyboard views | FLTK DnD (`FL_DND_*`, `FL_PASTE` with `text/uri-list`) on all platforms ✓ |
| Typing into the Newton | keyboard passthrough (`NSTextInputClient`) | custom widget: `FL_KEYDOWN`, `Fl::event_text()`, compose/IME ✓ |
| Help book (HTML), PDF guide | Help menu | `Fl_Help_View` window, or `fl_open_uri()` to the system browser/viewer ✓ |
| **Dragging entries or a screenshot out to Finder/Explorer** (file promises) | soup table, screenshot view | **missing in FLTK**: `Fl::dnd()` drags text only |

The only real gap is dragging files out of the app (D3). Everything else
is either available or a small custom widget.

### 1.7 Risks

- **Threads and the Newton object system.** The NewtonScript heap and GC
  are not thread-safe, and NCX today runs dock work on several GCD
  queues. The port will make it a rule that only the dock thread touches
  Refs and the UI thread only sees plain C++ data. The existing code has
  to be checked for Ref use on other queues while porting (step 5.1).
- **Windows and the framework.** The `long` Ref, the hand-defined
  `__LP64__` and the ROM assembler are fixed (0.1, 0.1b), but nothing has
  been built on Windows recently. The generated ROM data is a 16 MB C file
  of designated initializers: MSVC's C compiler should take it (C99/C11),
  clang-cl is the fallback.
- **RTFD has no cross-platform equivalent.** Notes that contain ink
  currently export as RTFD (an Apple bundle format with images inside).
- **macOS release builds** need Developer ID signing and notarization in
  CI. The secrets have to be set up once.
- **GitHub runners.** arm64 Linux and arm64 Windows runners exist now; the
  plan builds Intel macOS as part of a universal binary on an arm64 runner,
  so it doesn't depend on the Intel macOS runners being retired.

---

## 2. Target architecture

```
newton-connection/
  CMakeLists.txt            top level: C++17, FetchContent for FLTK, newton-framework, SQLite
  cmake/                    helpers (embedding images, bundle, icons, packaging)
  src/
    base/                   strings, dates, paths, prefs, logging, threads, progress, observers
    comms/                  transports, MNP, dock events, session, cursor   (no GUI)
    model/                  Device/Store/Soup/App/Entry + SQLite persistence (no GUI)
    dock/                   DockController and components                  (no GUI)
    translators/            Names, Dates, Notes, Works, Audio, Package, Text, Image
    platform/               per-OS code: serial port list, power, computer name, app folders
    ui/                     FLTK: main window, panels (.fl files and C++), widgets
    cli/                    ncx-cli: headless tool for tests and debugging
  resources/                icons (PNG), help, guide
  tests/                    unit tests, fake-Newton integration tests, golden files
  libical/                  as now (plain C)
  NCX/, NCX.xcodeproj       the Cocoa app, kept as reference only (not built)
```

The layers:
- **`ncx_core`** (static library) is base + comms + model + dock +
  translators. Its headers include no FLTK (or framework) headers, and
  nothing in it needs a display. It links FLTK only through `newton_ncx`'s
  drawing. Both `ncx-cli` and the GUI link it, so almost everything can be
  tested without a display.
- **`newton_ncx`** (static library in newton-framework) is the object
  system, NSOF, pipes, packages, compression, ROM data, and the QuickDraw
  functions, which draw with FLTK (0.5).
- **`newton_dock`** (static library in newton-framework) is MNP, the
  transports and DES (0.6).
- **`NCX`** (the app) is `ui/` plus `ncx_core` plus FLTK.

Threads:
- The **UI thread** runs FLTK.
- The **dock thread** owns the session, the components and all Refs.
- **One reader thread per transport** passes bytes to the dock thread.
- The dock thread reports progress and results through `Fl::awake()`
  callbacks that carry plain data.

---

## 3. The plan

Each step is meant to be one commit or one small PR. "Done when" is the
check. Phases 0–1 come first; 2–6 can partly overlap; 7 needs 5 and 6.

### Phase 0: newton-framework groundwork (in `newton-framework`, `dap_to_toolbox`)

- **0.1 Pointer-sized Refs.** ✅ Done 2026-10-16 (details in the
  framework's `Matt/CLAUDE.md`, section 5).
  - `Ref` is `intptr_t`; `VAddr`, `PAddr`, `OpaqueRef` are `uintptr_t`.
  - `NewtonArch.h` (preprocessor only) defines `NEWT_64BIT` from the
    compiler's own macros (`__LP64__`, `_WIN64`, `__SIZEOF_POINTER__`). It
    replaces every `#if __LP64__`. The hand-made `-D__LP64__=1` is gone.
  - The Ref macros (`Objects.h`), the NSOF writer's bit tests, and a few
    pointer casts and printouts use pointer-width types.
  - Done when: `newtc` and its tests pass on macOS arm64 as before.
    Result: all 21 suites pass (Debug with FLTK and ASan/UBSan; the
    Release build too).
- **0.1b The ROM data as C.** ✅ Done 2026-10-16 (D1).
  - ResMaker is a CMake target (`ResMaker`, not built by default) and
    writes C: `ROMData/forNewton` and `forNTK` hold `ROMData.c`,
    `ROMSymbols.c` and two headers instead of the `.s` files.
  - Its old assembler output matched the checked-in `.s` files (forNewton
    identical except two comment/alignment hand edits; forNTK identical
    after normalizing its old format). The C was then checked against the
    assembler data with a dumper: all 51,502 objects reachable from the
    9,729 RS roots and the magic pointer table identical (forNTK: 51,669).
  - Two old data bugs found and kept for now (B52, B53 in the framework's
    `Matt/CLAUDE.md`). All 21 test suites pass.
- **0.2 Framework CMake as a subproject.** ✅ Done 2026-10-16.
  - `CMAKE_CURRENT_SOURCE_DIR`/`BINARY_DIR`; `execute_process(ls …)` gone.
  - Options defaulting to "top-level project": `NEWTON_BUILD_TOOLS`
    (newtc, newtdump, ResMaker, NewtPlay) and `NEWTON_SANITIZE` (ASan).
    `NEWTON_BUILD_MESSAGEPAD` only on Intel Macs (ASM enabled only there).
  - macOS frameworks only `if(APPLE)`; no ASan/UBSan flags with MSVC.
  - CMake 3.15 minimum, so a parent's `set(NEWTON_BUILD_TOOLS …)` counts.
  - Done when: an outside project can pull it in with tools off. Result:
    `Test/cmake/test_subproject.py` does that with FetchContent and
    `FETCHCONTENT_SOURCE_DIR_NEWTON`, exactly as NCX will (nothing built or
    fetched by default; the tools build there when asked for).
- **0.3 The `newton_ncx` static library.** ✅ Done 2026-10-16.
  - 108 sources, measured with the linker from what NCX called of the old
    framework (outside drawing): objects, NewtonScript (interpreter,
    compiler), NSOF, stores, compression, packages, dates, Unicode, REP
    printing, the ROM data. No graphics yet (that's 0.5).
  - Public include dirs and compile definitions; NCX includes the same
    headers the old `NewtonKit.h` did, and they compile as C++17.
  - NCX initializes it with `InitObjectSystemCore()` (new: the object
    system without NTK's REP context, graphics or ROM scripts).
  - Needed on the way: the ROM data's 1,349 C function pointers moved to a
    separate generated file (`ROMFunctions.c`) that only newtc links, so
    the data no longer drags in views, graphics and sound; `ObjectSystem.cc`
    split into a core init and the rest; one text-measuring function moved
    from the object system to graphics.
  - Done when: a tiny test links only `newton_ncx` and round-trips a frame
    through `FlattenRef`/`UnflattenRef`. Result: `newton_ncx_test` (26
    checks over everything NCX uses) and a parent project in
    `Test/cmake/subproject` that links the library and round-trips a frame.
  - Found: compressed large binaries (`FLBAllocCompressed`) don't work on
    the framework's internal store (framework bug B55); NCX's MP3 and Works
    imports need them (step 6.5).
- **0.4 Windows and Linux builds of `newton_ncx`.** ✅ Done 2026-10-16.
  - GitHub Actions in newton-framework (`.github/workflows/newton_ncx.yml`)
    builds the library and its test and runs it on Linux x64/arm64 (gcc),
    Windows x64/arm64 (MSVC) and macOS universal (both slices). All green.
    MSVC compiles the library with pointer truncation as an error.
  - The big ones: a byte-order switch that only clang set (gcc and MSVC
    builds would have read every package and NSOF stream wrong), `RefVar`
    converting to a 32-bit `long` on Windows, binary files opened in text
    mode, and two latent bugs that only crashed on Windows (framework
    B56, B57). The rest: pointer casts, mismatched declarations MSVC
    catches at link time, POSIX-only calls (`mmap`, `realpath`, `link`,
    mach clock) given portable versions.
  - The library test now also checks NSOF bytes against a golden copy and
    reads a symbol out of a package, so a byte-order mistake can't hide
    behind a round trip.
- **0.5 QuickDraw on FLTK.** ✅ Done 2026-10-16.
  - Matt: no Cocoa or Quartz calls (MessagePad won't be implemented); FLTK
    calls if FLTK is linked. So instead of an interface with a Quartz
    version, the framework draws with FLTK directly.
  - `Graphics/QDHost.h`: a few primitives (colours, pen, lines, rects,
    round rects, ovals, arcs, polylines, polygons, RGBA images, PNGs,
    text). `QDHost.cc` draws them on FLTK's current surface when
    `NEWTON_QD_FLTK` is set, and does nothing otherwise.
  - The QuickDraw files (`QDDrawing.cc`, `DrawShape.cc`, `DrawImage.cc`,
    `DrawText.cc`, `DrawInk.cc`, `Ink.cc`) draw through it, with no y-flip
    and no CoreText. Text now uses the style's family, size and face, and
    is measured for real (it used to be Helvetica 10, always 16 pixels
    wide). Bitmaps of 1, 2, 4 or 8 bits, with gray tables and transfer
    modes, become RGBA. QuickDraw pictures of bitmaps (PICT) draw too.
    Images scale with each pixel a block.
  - `newton_ncx` contains the graphics. With `NEWTON_NCX_FLTK` it links
    `fltk::fltk` and `fltk::images`. That option is on by default when the
    parent project already has `fltk::fltk`, as NCX will (1.1).
  - For NCX: `InitDrawing` is gone. Make a window current (in `draw()`) or
    an `Fl_Image_Surface` (for PNG or RTF export), then call `DrawShape`,
    `DrawBitmap`, `DrawBits` (a screenshot, at 0,0), `DrawPolygon`,
    `InkDrawInRect`. Declarations: `DrawShape.h`, `Ink.h`.
  - Tested: `newton_ncx_test` draws shapes, bitmaps (1:1 and 2:1), a
    screenshot's bits, NCX's ink polygon and text into an
    `Fl_Image_Surface` and checks the pixels. CI runs it with FLTK on all
    five targets (Linux under Xvfb), plus one Linux job without FLTK.
  - Found and fixed framework bugs: black was drawn dark gray (B58), and
    a font frame's family was read wrong (B59).
- **0.6 Move the Dock link into a shared target.** ✅ Done 2026-10-16.
  - `newton_dock` is a static library with `MNP.cc`, `NTKTransport.cc` and
    `DES.c`. It is C++17, has no framework headers, and its headers are
    included by path (`"Matt/MNP.h"`). newtc, newtdump and its fake Newton
    link it; NCX will too.
  - `DockLink` and newtdump's small NSOF stay in newtdump: NCX has the
    framework's NSOF and its own dock protocol code (Phase 4).
  - On Windows the link and the transports are still stubs that say "not
    supported on Windows yet"; the framing, CRC and DES work there. Step
    3.1 brings Winsock and Win32 serial.
  - Tested: `newton_dock_test` checks the CRC, framing, DES against the
    textbook vector, and runs two MNP links over TCP on one machine (one
    plays the Newton, as Einstein does): link up, 8000 bytes both ways,
    disconnect. CI runs it on all six jobs, which was the first time
    `MNP.cc` and `NTKTransport.cc` were built on Linux. newtdump's tests
    pass, and the subproject test's program links `newton_dock` too.
  - Found and fixed framework bug B61: DES decoding encrypted again.

### Phase 1: NCX build skeleton and CI

- **1.1 Top-level `CMakeLists.txt`.** ✅ Done 2026-10-16.
  - C++17.
  - `FetchContent` for FLTK (pinned `release-1.4.5`), newton-framework
    (pinned commit), and the SQLite amalgamation.
  - `-DFETCHCONTENT_SOURCE_DIR_FLTK=~/dev/fltk-1.4.git` and
    `…_NEWTON=~/dev/newton-framework.matt.git` for local work.
  - FLTK as static libs, with Fluid built (needed for `.fl` files).
  - Done when: `cmake -B build && cmake --build build` builds an empty
    FLTK window that links `newton_ncx` and prints an NSOF round trip.
  - Result: FLTK `release-1.4.5`, newton-framework pinned to a commit on
    `dap_to_toolbox` (it builds only `newton_ncx` and `newton_dock` there,
    drawing with our FLTK), and the SQLite 3.54.0 amalgamation, checked
    with the SHA3-256 that sqlite.org publishes (as `ncx_sqlite3`, without
    run-time extensions). The version is 3.1.0 (the Cocoa app was 3.0.2).
    A fresh build takes about a minute on an M-series Mac.
  - `ncx_core`'s first function is a self test: a frame through NSOF, in
    an MNP frame, and in an SQLite blob, and back. `ncx-cli --selftest`
    and `NCX --selftest` run it without a window. The app prints it when it
    starts and shows a window with the menu bar (`Fl_Sys_Menu_Bar`:
    File/Quit, Help/About) that says whether the core works.
    `NCX --screenshot <png>` draws the window into a PNG and quits; CI uses
    it to show the UI on every platform.
- **1.2 Source layout.** ✅ Done 2026-10-16. Create `src/…` as in
  section 2. `NCX/` and `NCX.xcodeproj` stay for reference only; this
  branch builds pure FLTK (D8).
  - Now: `src/base` (`ncx_core`), `src/cli` (`ncx-cli`), `src/ui` (`NCX`),
    `cmake/` (the Info.plist), `resources/` (the icon), `tests/` (ctest).
    The other layers get their directories when their phases start, so
    there are no empty ones.
- **1.3 GitHub Actions: build.** Written 2026-10-16, not run yet
  (`.github/workflows/ncx.yml`). Matt: no CI runs for now unless needed
  (GitHub's macOS runners were stalling); it runs by hand
  (`workflow_dispatch`) and for pull requests. Locally the same build
  works, including a fresh universal one that fetches everything and
  passes ctest and the x86_64 slice under Rosetta.
  - Matrix: `ubuntu-24.04` (x64), `ubuntu-24.04-arm`, `windows-2022`
    (x64), `windows-11-arm`, `macos-15` (universal2:
    `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`).
  - Windows jobs use MSVC (`-A x64`, `-A ARM64`), or the ClangCL toolset
    if 0.4 needed it.
  - Upload build artifacts.
  - Check the runner labels when writing the workflow.
  - Done when: all five jobs are green.
- **1.4 GitHub Actions: tests.** Written with 1.3: ctest (the self
  tests of `ncx-cli` and `NCX`, the version), the x86_64 slice under
  Rosetta, a screenshot of the window per platform (Linux under Xvfb),
  and the installed app with its screenshot as artifacts.
  - `ctest` on every job.
  - The x86_64 macOS slice runs under Rosetta (`arch -x86_64`).
  - Done when: the NSOF test runs everywhere.

### Phase 2: base layer (replacing Foundation)

✅ Done 2026-10-16, locally (CI by hand, see 1.3). `src/base` and
`src/platform`, tested by `tests/base_test.cc` (`ncx_base_test`, 68 checks
in 9 ctest sections), which also passes under ThreadSanitizer and under
AddressSanitizer with UBSan.

- **2.1 Strings:** ✅
  - UTF-8 `std::string` throughout.
  - Ref ↔ UTF-8 helpers (replace `MakeNSString`, `MakeString…`).
  - A MacRoman table (the code uses `NSMacOSRomanStringEncoding` 7
    times).
  - Ports of the `NSString-Extensions` that are actually used.
  - Result: `base/Strings.h`: UTF-16 ↔ UTF-8 (surrogates; bad input
    becomes U+FFFD), MacRoman → UTF-8 (only symbol names used it), the
    two extensions that are used (`ReplaceAll` for Works' CSV quotes,
    `ReplaceControlCharacters` for its summaries; the other three were
    unused), and line ends both ways (CR on the Newton, LF on the desktop).
    `base/NewtonStrings.h` (the one header with framework types):
    `UTF8FromString`, `UTF8FromStringLF`, `MakeStringFromUTF8` (LF becomes
    CR, as `MakeString` did), `UTF8FromSymbol`.
- **2.2 Dates:** ✅
  - NewtonTime (minutes since 1904) ↔ `time_t` ↔ Core Data time (seconds
    since 2001).
  - Formatting for file names and display (replaces `NSDateFormatter`
    uses).
  - Result: `base/Dates.h`: 64-bit seconds since 1970 in the middle; the
    conversions are plain arithmetic as before (a Newton time is read as
    if its clock ran on UTC; worth checking against a real Newton when the
    dock works). Calendar dates in UTC (own code, any year) and local time.
    `FormatDate` "Oct 16, 2026", `FormatTime` "3:04 PM", `FormatDateTime`,
    `FormatRelativeDateTime` ("Today/Yesterday/Tomorrow at", for the last
    backup), `FormatForFileName` "2026-10-16 15.04". English only, as the
    app's only localization was.
- **2.3 Paths and app folders (`platform/`):** ✅
  - macOS keeps `~/Library/Application Support/Newton Connection`, so
    existing files are found.
  - Windows: `%APPDATA%\Newton Connection`.
  - Linux: `$XDG_DATA_HOME/newton-connection`.
  - Result: `platform/AppFolders.h`, plus the log file
    (`~/Library/Logs/NewtonConnection.log` as before,
    `%LOCALAPPDATA%\Newton Connection`, `$XDG_STATE_HOME/newton-connection`).
    `NCX_DATA_DIR` and `NCX_LOG_FILE` override them (tests, a portable
    copy). Paths are `std::filesystem` (macOS 11 and later now:
    `CMAKE_OSX_DEPLOYMENT_TARGET`), as text UTF-8.
- **2.4 Preferences:** ✅ `Fl_Preferences`, same key names as
  `PreferenceKeys.h`.
  - Result: `base/Prefs.h`: `Prefs::App()` (FLTK's file per user:
    vendor `newtonresearch.org`, app `Newton Connection`; the old
    NSUserDefaults aren't read), `Prefs::InMemory()` for tests, typed
    get/set under a lock (the dock thread reads them too), and the Cocoa
    app's fallbacks (TCP port 3679, 38400 bps; it registered no defaults).
- **2.5 Logging:** ✅ `REPprintf`/`NSLog` → one logger with levels and an
  optional log file (prefs `LogToFile`, `LogLevel`).
  - Result: `base/Log.h`: the app's levels (0 none, 1 minimum, 2 full, 3
    developer), `NCX_LOG_MINIMUM/FULL/DEVELOPER` for the old
    `MINIMUM_LOG`..., stderr, the log file with time stamps, a sink for a
    log window and tests. `StartLogging()` (`base/Core.h`) sets it up from
    the preferences; the app calls it. Still to do: the framework's
    `REPprintf` output (backup dumps) goes to stdout, not into the log.
- **2.6 Threads:** ✅
  - `SerialQueue` (a thread with a task queue), `Semaphore`, `Timer` (for
    the tickler).
  - `Progress` with a cancel flag.
  - `Observers` (replacing the six `kDock…Notification`s), delivered on
    the UI thread through a hook the UI installs.
  - Result: `base/Threads.h` (`SerialQueue` with `async`, `sync` that runs
    at once on its own thread, `stop` that runs what is queued;
    `Semaphore`; `Timer`, restartable and stoppable from its own task;
    `RunOnMainThread` with `SetMainThreadRunner`, which the app sets to
    `Fl::awake`, after `Fl::lock()`), `base/Progress.h`,
    `base/Observers.h` (`NotificationKind`: the six, and
    `kSerialPortChanged`; a `Notification` carries the document, the
    operation and the error, as the old userInfo did).
- Done when (all of 2.x): unit tests for strings, dates and queues pass in
  CI. Locally they pass; CI waits (1.3).
- Found on the way: `NCX --screenshot` waited forever on a locked screen
  (a timer that macOS throttles while the window can't be seen); it draws
  at once now, since the image surface needs no screen.

### Phase 3: comms layer (no GUI)

✅ Done 2026-10-16. CI (by hand, on a branch `ncx-3`): green on Linux
x64/arm64 and Windows x64/arm64 (21 ctests: MNP over Winsock, the listener,
plain TCP, packages, `ncx-cli info` against the fake Newton); macOS stopped
at a timer test (a slow runner: the timer caught up missed runs in a burst;
fixed, checked locally under load and ThreadSanitizer). `src/comms`
(in `ncx_core`), `platform/SerialPorts`, and in newton-framework
`newton_dock` on Windows. Tested by `tests/comms_test.cc` (6 ctest
sections against the fake Newton), `ncx_base_test serial`, and two ctests
that run `ncx-cli info` against `ncx-fake-newton`; clean under
ThreadSanitizer and ASan/UBSan.

- **3.1 Transports.** ✅
  - Build on `newton_dock`'s `NTKTransport` (tcp server for Einstein and
    the Newton's network option, serial, Einstein PTY), plus the Einstein
    FIFO endpoint (POSIX).
  - Add Windows serial (Win32, overlapped) and Winsock TCP.
  - Use one reader thread per transport, so Windows serial needs no
    `select()`.
  - Result: there are two kinds of TCP docking (Matt). Einstein's serial
    port emulated over TCP carries the whole serial protocol (MNP); a
    Newton with the Internet Enabler (NIE driver, NE2000 card) sends the
    Docking Protocol over plain TCP, where TCP takes MNP's place. Both come
    to port 3679; the listener tells them apart by the first byte (MNP
    starts with SYN, a dock event with 'newtdock'). `newton_dock` got
    `tcp-all:<port>` (all interfaces, for a Newton on the network; `tcp:`
    stays this machine only, for Einstein) and `MNPLink::start(transport,
    firstBytes)` (MNP on a transport someone else opened and read from).
    Einstein's named pipes are NCX's own transport (`einstein-pipes`,
    macOS only: Einstein makes them only there). On Windows `newton_dock`
    keeps its descriptor model: TCP is Winsock (sockets work with
    `select()`), a serial port is overlapped Win32 I/O with a reader thread
    that passes the bytes through a socket pair (its `fd()`), and MNP's
    wake-up pipes are socket pairs too (`NTKMakeWakePair`). newtc and
    newtdump get it as well. Windows serial isn't tested (no ports on CI).
  - `src/comms/Connection.h`: a byte stream to a Newton, MNP or plain.
- **3.2 Listening on all transports at once.** ✅ `src/comms/Listener.h`
  (NCEndpointController): every target at once, the first Newton wins,
  the rest are closed; targets that can't be used are logged and skipped.
  The default (`ncx-cli`): `tcp-all:<TCPIPPort>` and `einstein-pipes`.
- **3.3 Serial port list (`platform/`):** ✅ IOKit (macOS), `/dev`
  scanning (Linux), `SERIALCOMM` registry (Windows), for the preferences
  panel. `platform/SerialPorts.h`: macOS the callout devices as before;
  Linux the tty devices with hardware behind them, named by
  `/dev/serial/by-id`.
- **3.4 MNP.** ✅ Use `newton_dock`'s `MNP.cc` (D2).
  - Result: nothing of NCX's MNP had to be carried over. Its LR is byte
    for byte newtdump's `kDesktopLR`; newtdump's has what NCX's lacked
    (retransmission, keep-alive LAs, the Newton's credit); NCX read an LA's
    credit 0 as "resend" (it means "wait"). Smaller chunks and reconnecting
    belong to the operations (Phase 5, 6).
- **3.5 Dock events and session.** ✅
  - `comms/DockProtocol.h` (the commands as `Cmd("rtdk")` constants, made
    from the Cocoa header by a script; `NewtonInfo`; the errors),
    `DockEvent` (encoding, an incremental parser that also finds the end
    of an event of no set length, a description for the log), `DockRefs`
    (NSOF in events), `EventQueue` (NCDockEventQueue: a reader thread),
    `Session` and `Cursor` (NCSession, NCCursor: all their requests;
    `DockFailure` instead of `exComm`; a tickler that says 'helo' after 30
    quiet seconds; `sendPackage` in pieces as MNP acknowledges them, with
    progress and cancel), `Handshake` (do_rtdk, do_name, do_ninf, do_pass:
    the desktop info, the icons, the password both ways with two retries;
    Windows says it is a Windows desktop, as NCU did). NCBuffer, CRC,
    ChunkBuffer weren't needed (MNP is newton_dock's).
- **3.6 `ncx-cli` v0:** ✅ `ncx-cli info [--target ...] [--password ...]
  [--wait ...] [--log 1]` listens, docks, takes control (`dsnc`), prints
  the Newton (name, machine, ROM, RAM, screen, protocol), its stores and
  their soups, and disconnects (`opdn`, `disc`).
- **3.7 Fake Newton for NCX.** ✅
  - `tests/FakeNewton.cc`, `ncx-fake-newton`: connects as Einstein's serial
    port (MNP) or as the Internet Enabler (plain), docks, and answers from a
    fixture (a store, two soups, entries, a query's cursor, global
    functions, packages). It runs as a program of its own: the object
    system belongs to one thread (the dock's), so a fake Newton on a thread
    of the test would share it (ThreadSanitizer saw that).
  - Done when: a CI test runs `ncx-cli info` against it on all
    platforms, and `ncx-cli info` works against Einstein locally.
  - `ncx-cli info --target tcp:3679` against Einstein (its serial port as
    a TCP client) on Windows 11: docks, lists stores and soups, and
    disconnects, twice in a row (Matt, 2026-10-16). The Newton then says
    "The connection was stopped because your PC disconnected": its words
    for a desktop's `disc`, as after the Cocoa NCX's disconnect.
  - Still to do: `ncx-cli info` against a real Newton (Matt).
- Found in newton-framework: DES's bit permutation shifted `1 << 31` (an
  int; UBSan; B67), MNP's settings read once raced when two links started at
  once (ThreadSanitizer; B68). Both fixed.
- Found on Windows: MSVC reads sources in the ANSI code page, so NCX's
  sources stay ASCII (escapes for anything else); test ports stay below
  40000 (Windows hands out 49152 and up, and CI runners reserve blocks
  there). `base/Threads.h`'s Timer no longer makes up for missed runs.

### Phase 4: data model and persistence (no GUI)

✅ Done 2026-10-16, locally (no CI run yet). `src/model` (in `ncx_core`),
`ncx-cli device` and `ncx-cli convert`. Tested by `tests/model_test.cc` (10
ctest sections, one of them on a backup the Cocoa NCX made); 31 ctests,
clean under ThreadSanitizer and ASan/UBSan.

- **4.1 Model classes:** ✅
  - `model/Records.h`: plain structs `DeviceRecord`, `UserInfoRecord`,
    `StoreRecord`, `AppRecord`, `SoupRecord`, `EntryRecord`, one field per
    column; a row is its `Z_PK`, a reference the other row's. The device's
    `NewtonInfo` (`ZINFO`, 72 bytes, little-endian as the Cocoa app kept
    the struct) and what NCX showed of it (DeviceId, machine, ROM).
  - `EntryRecord` keeps its NSOF blob (`refData`); `EntryFrame()`
    unflattens it when asked.
  - `model/NewtonModel.h` (the dock thread: Newton objects): what
    NCDocument, NCStore, NCSoup and NCEntry did with the frames a Newton
    sends. `AddStore` (by signature), `FindOrAddApp`, `MakeSoup` (from the
    soupDef), `AddEntry` (titles from the soup's columns: the Cocoa app's
    `slot.plist` as a C++ table, `SoupColumns`), `AddImportedEntry`
    (desktop-made entries get ids from 1070000000 on), `UpdateEntry`.
  - Imported entries' ids don't fit a Newton integer (30 bits). The
    64-bit Cocoa app kept them anyway, NSOF stored their low 30 bits, and
    it read them back unsigned. `UniqueIdOf`/`MakeUniqueId` do the same, so
    the NSOF stays byte for byte what the Cocoa app wrote.
  - `model/Restore.h`: NCRestoreInfo. For each store of the connected
    Newton (Internal first): the backed-up stores of its kind, with their
    apps (but Packages) and packages. What the user picks is the UI's.
  - The UI-only properties (`image`, `icon`, `status`) are left to `ui/`.
- **4.2 SQLite store:** ✅ `model/DeviceFile.h`
  - Reads and writes the Core Data schema from 1.5 (`model/CoreDataModel.h`
    has the exact statements, the entities, the model's hashes). A new row's
    `Z_PK` is `Z_PRIMARYKEY.Z_MAX + 1`, its `Z_ENT` the entity's number, its
    `Z_OPT` 1, then +1 per change. Deleting a soup or a store takes its
    entries and soups along (the model's delete rules only nullify).
    `Z_METADATA` (a binary plist, `model/Plist.h`) stays as found, but for
    NewtonId and NewtonName (decision D5). New files: WAL, incremental auto
    vacuum, as Core Data makes them. An open checks the model's hashes.
  - `DeviceFile::Transaction` replaces `save:`; there are no per-thread
    contexts.
  - Checked with Core Data on macOS (by hand, once; `xcrun momc` of
    `NCX/Store.xcdatamodel`): it opens a file NCX wrote, fetches every
    entity with its relationships, and saves changes into it; NCX opens a
    file Core Data made, and the one it changed. Core Data leaves
    `Z_MODELCACHE` empty, as NCX does.
- **4.3 Device chooser and legacy backups:** ✅
  - `model/DeviceList.h`: the `.newtondevice` files in a folder with their
    NewtonId and NewtonName (from `Z_METADATA`, read only), the file for a
    Newton that connects (by NewtonId, else a new `<DeviceId>.newtondevice`:
    NCDocument setDevice).
  - `model/BackupImport.h`: BackupDocument. A Newton Connection 1 backup
    (`.nbku`, either byte order) into a device file in memory;
    `DeviceFile::saveAs` writes it out. `ncx-cli convert <nbku> <file>`.
- **4.4 Tests:** ✅
  - `tests/model_test.cc`: plists; a file made, written, reopened
    (`Z_MAX`, `Z_OPT`, WAL, read only, other files refused); deletes and
    `cropSoup`; entries (updates, imported ids and their NSOF); titles;
    `.nbku` import (made by the test, both byte orders, cut short); the
    device list; large binaries; restore choices.
  - The real backups in `~/Library/Application Support/Newton
    Connection/` (Matt's: read only, counts only): `ncx-cli device <file>
    --check` reads every entry of both (117), and every package is a
    package.
  - `tests/fixtures/einstein.newtondevice` (Matt, 2026-10-16: no personal
    data): a backup the Cocoa NCX made from Einstein (MessagePad 2000, an
    internal store and a flash card; 21 soups, 18 packages). The
    `fixture` section reads it (every entry, every package decompressed;
    titles, classes, ids and lastBackupIds as the Cocoa NCX made them),
    changes it and reopens it. It has 22 entries in no soup: the Cocoa NCX
    deleted the card's soups but not their entries. Core Data opens it as
    copied. The Cocoa NCX connects to Einstein only through the named pipes
    (its TCP endpoint is plain TCP, Einstein's TCP driver sends MNP).
- Found in newton-framework: large binaries (VBOs) in NSOF, which a Newton
  sends for compressed text, ink and packages, crashed or failed: the
  reader mapped them into virtual memory the host doesn't have (B70; now
  read into plain binaries, decompressed). The package store's list of
  decompressors had every other one missing (B69). `MNPLink::pending()`
  raced with the link's thread (ThreadSanitizer, the package test; B71).
  `IsString` was false for the string classes (a Names card's company and
  phones: no title; B72). The Zippy decompressors overran their buffer
  (ASan, the fixture's packages; B73). On Windows NCX didn't link: MSVC
  keeps unreachable code, and the relocation code's (after an early return)
  called a function newton_ncx doesn't have (B74). A Debug build on Windows
  stopped at MSVC's run-time check for variables read before they are set
  (B75: 49 in the framework, found with clang's uninitialized warnings;
  NCX's own sources have none). CI builds RelWithDebInfo, which doesn't
  check. All fixed: on Windows 11 all 31 ctests pass in a Debug build
  (Matt, 2026-10-16).

### Phase 5: dock controller and components (no GUI)

In progress. Done so far: 5.1; 5.2.1 and 5.2.2 (against Einstein on
Windows, Matt, 2026-10-16/17); 5.2.3 to 5.2.8 against the fake Newton
(backup and restore round-trip its stores), 5.2.4 against Einstein too. 55
ctests, clean under ThreadSanitizer and ASan/UBSan.

- **5.1 `DockController`:** ✅ (the plan below; what came of it here)
  - `src/dock/DockController.h`: a dock thread that listens, docks
    (`Handshake`), and serves the session: operations the desktop posts
    (lambdas, run in order on the dock thread) and requests the Newton
    makes on its own (handlers by command; 'opca' with nothing to cancel is
    acknowledged, unknown ones answered 'unkn'). Then it listens again, or
    ends (`once`, for ncx-cli). Events (listening, docked, undocked,
    status) come on the dock thread; the UI passes them on. `ncx-cli info`
    runs on it now.
  - Newton exceptions and C++ exceptions: the framework throws with
    longjmp, so a C++ exception must not leave a `newton_try` (its handler
    would stay behind), and an uncaught Throw ends the program.
    `base/NewtonCall.h` (`CallNewton`) runs code where both are safe; the
    dock thread runs every operation and handler through it (a Newton
    exception fails the operation, a `DockFailure` ends the session), and
    the `.nbku` import too.
  - Protocol extensions: `src/dock/extensions/<id>.nsof`, embedded at
    build time (`cmake/EmbedExtensions.cmake`, `dock/Extensions.h`). NCX's
    own are NewtonScript (`<id>.ns`), compiled with newtc by the optional
    target `ncx_extensions` (`-DNCX_NEWTC=<newtc>`); the NSOF is checked in,
    so a build needs no newtc (as the framework's ROM data). See
    `src/dock/extensions/README.md`.
  - Port `NCDockProtocolController`.
  - Operation state, cancel, progress, the install queue, folder browsing
    for the Newton (`buildFileList` and friends).
  - Check every component for Ref use outside the dock thread and fix it
    (risk in 1.7).
  - Protocol extensions: NCX loads prebuilt `.stream` files (`NCX/NTK`:
    `scrn`, `snap`, `reqp`, `gent`, `pfnd`, …) from its bundle. Embed them
    into the binary with a CMake step instead. Extensions that get rewritten
    (see 5.2) are NewtonScript compiled by newtc at build time, as
    `cmake/NewtDumpExtension.cmake` does for newtdump.
- **5.2 Components, in this order** (each one gets an `ncx-cli` command
  and, where possible, a fake-Newton test):
  1. screenshot (`ncx-cli screenshot out.png`). The current `scrn`/`snap`
     extensions send the screen in very large chunks. On real devices that
     can overwhelm the Dock/MNP link and fail. Rewrite them to send smaller
     blocks, the way newtdump's `NDmp` extension does.
     ✅ `dock/Screenshot.h`: `NCss` (`NCss.ns`) takes the screen with the
     Toolkit app's `ScreenShotFn`, as 'snap' did, but a few rows at a time
     (about 2 KB an answer), each a shot of its own, so nothing stays on the
     Newton between requests; white rows aren't sent (`BinEqual` with an
     empty binary). 'scrn' (taken over: hides the Dock slip) stays. The
     Newton needs the Toolkit app (`NCX/Extras/Toolkit.newtonpkg`);
     without it `begin()` says so (-28010). The bits become 8-bit gray (0
     white ... the highest value black), `ncx-cli screenshot` writes a PNG
     (`fl_write_png`); `--block 0` takes it in one piece, as 'snap'.
     Against Einstein on Windows (Matt, 2026-10-16): the PNG came complete.
     Found on the way: keeping the Toolkit's bits on the Newton and reading
     them later didn't answer; keeping a copy filled the NewtonScript heap
     (NTK's own Toolkit takes a screenshot in parts when it runs out of
     memory: `ScreenShotFn` takes a rectangle), hence the rows. Einstein's
     TCP serial port on Windows sent 4 bytes per timer tick (15.6 ms), about
     250 bytes/s; Matt fixed it in Einstein. Still to do: a real Newton.
  2. ROM dump (`ncx-cli rom out.bin`). Take the approach from newtdump
     (block size, resume after a reconnect).
     ✅ (fake Newton) `dock/MemoryDump.h`: newtdump's extension itself
     (`NDmp`, compiled from newton-framework's `Matt/NewtDump/Newton/
     Extension.ns` by `ncx_extensions`: the bytes newtdump sends), physical
     memory in 1 MB windows, 2 KB reads, a uniform block as one byte (an
     integer comes as a 4-byte long, not NSOF). `DumpMemory` writes a range
     to a file, with progress, cancel (on the desktop, or Disconnect on the
     Newton: 'opca' -> 'ocaa'), and resume after the whole blocks the file
     has. `ncx-cli rom <file> [--size] [--address] [--block] [--resume]`,
     8 MB from 0 by default (every NewtonOS 2 machine: MessagePad 2x00,
     eMate 300, Einstein). Reconnecting by itself (newtdump does) is left to
     the caller: `--resume` goes on. Against Einstein on Windows (Matt,
     2026-10-17): about 8 KB/s. Still to do: a real Newton.
  3. package install (`ncx-cli install a.pkg b.pkg`), with `PkgPart`
     ✅ (fake Newton) `dock/Packages.h`: `PackageInstaller` as the Cocoa
     app's component: desktop in control, `rins`, then per package the
     Cocoa app's `pfnd` extension (installed already? `pkno`, `pkya`,
     `tran`): replaced only if the caller says so (`rmvp` first), otherwise
     left; sent to the default store with progress (a part of all of them:
     `Progress::setParent`), cancel, Disconnect on the Newton ('opca').
     NewtonOS 1 just takes them, and the session ends. A package's name
     from its directory (no framework needed; `PkgPart` wasn't). `ncx-cli
     install <package>... [--replace]`.
     The Newton's own Install (5.1's folder browsing): `dock/FileBrowser.h`
     answers `rtbr`, `dpth`, `spth`, `gfil`, `gfin` (and `rali`, as the
     Cocoa app: nothing) and installs what it asks for (`lpfl`). The path
     is [computer, disk, folders]; it shows the folders and the files of
     the kind asked for ('packages, 'syncFiles: backups, 'import: none
     until Phase 6), no hidden ones. `ncx-cli serve [--folder]` docks and
     waits for the Newton. Still to do: against Einstein and a real Newton.
  4. backup, full and selective (`ncx-cli backup`), which writes the
     `.newtondevice`
     ✅ (fake Newton) `dock/Backup.h`, as the Cocoa app:
     - `UpdateDevice` (do_GALS): after docking, the Newton in its file
       (`OpenDeviceFileFor`): name, info, its stores, and what Gestalt says
       (OS version, manufacture date, processor); the clock if asked.
     - `Backup`: the desktop asks (`ssyn` with the desktop in control) or
       the Newton does (Dock's Backup: `ssyn` from it); the sync options
       name the stores (and whether packages); per store its apps (System
       first, Packages last, only if wanted), per soup: a new one whole
       (`snds`), a known one changed info and indexes, then what changed
       since `lastBackupId` and the ids of the rest (`bksp`, `base`,
       `bids`), then synchronised; `NCKLastBackupTime` in the soup's info,
       in the file and on the Newton. Each soup a transaction (what came
       stays). Afterwards the device's backup date and error.
     - Synchronising, as the Cocoa app does: entries the Newton no longer
       has leave the file; entries made on the desktop (imported) go to the
       Newton and take the ids it gives; and entries the Newton has that
       the file doesn't are deleted on the Newton (taken for deleted on the
       desktop). The Cocoa app deleted those at once, also when the file was
       only incomplete. Decided with Matt (2026-10-17): they wait until all
       soups are backed up (`DeletionPlan`); never in a soup whose backup
       never finished (no `NCKLastBackupTime`: one that broke off, a
       converted `.nbku`), which is a warning (the file is incomplete or
       damaged); a few on purpose (at most 10, no soup losing more than a
       fifth) are deleted without asking; otherwise the user is asked
       ("About to delete 120 entries": Delete / Only Add: `setDecide`, a
       dialog in Phase 7; `ncx-cli backup` asks on the terminal, or takes
       `--delete`, `--only-add`).
     - Selective backup is the sync options': the Newton's Backup slip says
       which stores and whether packages; from the desktop everything.
     `ncx-cli backup [--folder]` (into the file of the docked Newton in the
     folder, the app's by default); `ncx-cli serve [--backups]` answers the
     Newton's Backup. Against Einstein on macOS (Matt, 2026-10-17): 20
     soups, 65 entries, all readable, Core Data opens the file; the soups
     match the Cocoa NCX's backup of that Einstein but for the ones it kept
     from older sessions (it never removes a soup gone from the Newton).
     Found on the way: events of no set length (`cgfn`) must not be padded
     (the Newton reads the padding as the next header: -28016, bad header;
     since Phase 3, unseen until the first global function call), and a
     refused request now says so ("the Newton refused 'stme': -28016 (bad
     header)", "the Newton disconnected after 'ssyn'"). The second,
     incremental backup (Matt, 2026-10-17) failed at a soup's `sinf`: the
     Newton rejected the same MNP frame again and again. Einstein's serial
     drivers wrote everything the host sent into the receive DMA, also past
     its count, overwriting bytes NewtonOS hadn't read; a whole MNP frame
     comes at once over TCP. Fixed in Einstein (all its serial drivers read
     no more than the DMA can take, and wake when NewtonOS starts it again);
     then the incremental backup went through (20 soups, 2 entries came). A
     big frame sent right after one of the Newton's is still refused once
     and taken when sent again at once (its MNP, harmless). Still to do: a
     real Newton.
  5. restore
     ✅ (fake Newton) `dock/Restore.h`, the Cocoa app's desktop half: what
     to restore from `model/Restore.h`'s choices (a store of the Newton, a
     backed-up one of its kind, apps, packages); desktop in control, `rrst`,
     `sver` (1.x or 2.x data, manufacturer, machine), the Newton's store;
     per app and soup: deleted if there, made again (indexes, info,
     signature), filled with the entries with their ids (`auni`); entries
     made on the desktop are added (`adde`) and take the Newton's ids
     (the Cocoa app sent them with their temporary ids). Packages removed
     and installed again (their `pkgRef`). `ncx-cli restore [--folder]
     [--to] [--from] [--apps] [--packages] --yes` (without `--yes` it only
     says what it would restore). Left out: the Newton asking to restore
     (`rsfl`, `grop`, `ropt`, `rall`): in the Cocoa app it reads the old
     `.nbku`-style files only, and was unfinished. Still to do: against
     Einstein and a real Newton.
  6. import of entries and soup modification (delete entries or packages)
     ✅ (fake Newton) `dock/SoupEditor.h`, the Cocoa app's do_IMPT,
     do_DIDL and do_DPKL: with the Newton docked, entries made on the
     desktop (imported ids) go to it (`adde`) and take its ids; entries are
     deleted on it (`dele`) and in the file, packages removed by name
     (`rmvp`, after setting their store and then the default one, as the
     Cocoa app); what the Newton refuses stays in the file, entries never
     sent only leave the file. Without a Newton the UI changes only the
     file and the next backup does the rest (it sends what was made, and
     deletes what the file no longer has: `DeletionPlan`); packages only
     with the Newton docked, as the Cocoa app. `ncx-cli add [--store]
     [<soup>...]`, `ncx-cli delete [--store] <soup> <id or title>...`.
     Importing a file through a translator, on the desktop or the Newton's
     Import (`impt`, `tran`), comes with the translators (Phase 6) and uses
     `add`. Still to do: against Einstein and a real Newton.
  7. keyboard passthrough (an API only; `ncx-cli type "text"`)
     ✅ (fake Newton) `dock/Keyboard.h`, the Cocoa app's
     NCPassthruComponent: the desktop starts it (in control, `kybd`, the
     Newton answers `kybd`) or the Newton does (Dock's Keyboard: `kybd`
     from it); text as `kbdc` (one character and the command key) or
     `kbds` (big-endian UniChars, in pieces of 128), unanswered; a return
     is a CR, a character beyond 16 bits a '?'; the keys that aren't text
     as NCPassthruView mapped them (`newtonkey::`: arrows, backspace, F1
     ...). The desktop ends it (`opdn`), or the Newton (`opca`, acknowledged).
     No timeout while it's on. Calls from any thread post to the dock
     thread; the UI hears on and off (`setChanged`). `ncx-cli type <text>...
     [--file]`. Still to do: against Einstein and a real Newton.
  8. the Newton 1 session (`Newton1Component`)
     ✅ (fake Newton) `dock/Newton1.cc`: the same `Backup`, `Restore` and
     `UpdateDevice` take the old protocol when the Newton is NewtonOS 1, so
     `ncx-cli backup`/`restore` (and the UI) need nothing else. No desktop
     control (`setDesktopControl` does nothing), no Gestalt or global
     functions, no sync options: every soup of every store, each in an app
     as NewtonOS 2 has it (`Newton1AppName`, the Cocoa app's
     `appslot.plist`) or of its name; the ids of all entries (`gids`), then
     those changed since the soup's last backup (`gcid`), fetched one by one
     (`rete`); synchronised as for NewtonOS 2 (`DeletionPlan`); no packages
     (as the Cocoa app). An entry made on the desktop is added with `adde`;
     if the Newton answers with a result only (the Cocoa app expected that
     of NewtonOS 1), it leaves the file and comes back with its id at the
     next backup (`SendImportedEntry`, now shared by backup, restore and
     `SoupEditor`). Restore: no `rrst`, `sver` or signature; entries by
     `adde`. Keyboard passthrough and screenshots are NewtonOS 2 only. The
     fake Newton's `--newton1` ends with an error at any NewtonOS 2
     request. Package install for NewtonOS 1 came with 5.2.3. Still to do: a
     real NewtonOS 1 Newton (MessagePad 120/130; Einstein runs 2.x only).
  9. later: PCMCIA card dumps (CIS, attribute and common memory), taken
     from newtdump (`CIS.cc`, the `PhysMap`/`PhysRead`/`CardPower`
     natives).

  `SyncComponent` is dropped.
- Done when: every command works against Einstein, and backup/restore
  round-trip a Newton's stores.

### Phase 6: translators (no GUI)

- **6.1 Registry:** the `NCXTranslator` interface in C++, with
  translators registered at static-init time (no runtime plugin loading).
  The import/export file type tables come from the old `*-Info.plist`
  files.
- **6.2 Text-only translators first:** Names (vCard), Dates
  (iCalendar via the vendored libical), plain-text Notes, Package, Text.
- **6.3 Drawing:**
  - The framework draws on FLTK since step 0.5: make an
    `Fl_Image_Surface` current and draw into it.
  - Ink decompression and drawing (`InkDrawInRect`), shapes, bitmaps.
  - Export as PNG (`fl_write_png`).
  - Note: `Fl_Image_Surface` needs a display on Linux, so CI uses
    `xvfb-run` for these tests (as the framework's CI does since 0.5).
- **6.4 Rich text:**
  - An RTF writer (styles, fonts, paragraphs, tabs, embedded PNG via
    `\pngblip`) for Notes, checklists, outlines and Works.
  - Replace RTFD (decision D4).
  - A minimal RTF/text reader for import.
- **6.5 Audio:** Recordings → WAV/AIFF; MP3 import with dr_mp3/minimp3 and
  ID3 tags, converted to Newton sound frames and compressed with
  `FLBAllocCompressed`. That needs a store with large objects first:
  framework bug B55 (the internal store has none).
- **6.6 Golden-file tests:** export fixture entries and compare them with
  files saved from the current macOS NCX (text formats exactly, images by
  pixel tolerance).

### Phase 7: FLTK user interface

- **7.1 UI conventions.**
  - Fluid (`.fl`, compiled by `fltk_run_fluid`) for the fixed panels:
    preferences, about, device, store, soup header, package info,
    screenshot, keyboard.
  - Hand-written C++ for the main window shell and custom widgets.
  - Each panel is a class with `update(const Model&)`; there are no
    bindings.
  - Images are embedded PNGs (like the framework's `EmbedImages.cmake`).
- **7.2 Main window:**
  - Toolbar (image buttons, plus the segmented pane toggles).
  - `Fl_Tile` with sidebar (`Fl_Tree`), content area (panels swapped by
    the selection), and inspector.
  - Status line and progress.
  - Remember sizes in prefs.
- **7.3 Menus.** NCX opens one window per `.newtondevice` document, so
  there is no single main window to hold a menu bar (D9).
  - Write the menu once: an `Fl_Menu_Item` table built from the 59 items in
    `Main.storyboard`, with `FL_COMMAND` shortcuts. Callbacks send a
    command ID to the active document window, or to the app when no window
    applies (About, Preferences, Quit, Install Package…).
  - macOS: one global `Fl_Sys_Menu_Bar`. It exists with no window open,
    and adds a Window menu with `Fl_Sys_Menu_Bar::create_window_menu()`.
  - Windows and Linux: each document window has its own `Fl_Menu_Bar` row
    above the toolbar, built from the same table. The window layout
    reserves that row only on these platforms. Other windows (preferences,
    help, dialogs) have no menu bar; they use buttons.
  - Enable/disable items from the dock state and the window's selection
    (replacing `validateUserInterfaceItem`), refreshed when the active
    window changes. On Windows and Linux every window refreshes its own
    bar.
  - Closing the last document window: macOS keeps the app running (menu
    still there); Windows and Linux show the device chooser again, and
    quit from there.
- **7.4 Panels** (one step each): device (with the package list and
  dropping packages), store (capacity indicator), soup (an `Fl_Table_Row`
  subclass with columns per soup, sorting, multiple selection, delete and
  export), package info, screenshot (show, save, copy to clipboard), and
  keyboard (passthrough widget, paste, dropping text).
- **7.5 Inspector:** an entry shown as styled text (`Fl_Text_Display` with
  a style table) plus the rendered image for ink and drawings.
- **7.6 Preferences window:** `Fl_Tabs` for general, connection
  (serial port list, baud rate, TCP port, password), and backup/sync
  options.
- **7.7 Dialogs and flows:** device chooser at start, backup/restore/import
  dialogs, delete warnings with "don't show again", progress with cancel,
  error alerts.
- **7.8 Drag and drop in:** files onto the window, device, store and soup
  panels; text onto the keyboard panel.
- **7.9 Help:** HTML help in an `Fl_Help_View` window (or open in a
  browser), and the PDF guide through `fl_open_uri`.
- **7.10 Platform touches:**
  - Keep the machine awake during transfers.
  - macOS bundle with `Info.plist` and icon (`fltk_set_bundle_icon`).
  - Windows `.rc` with icon and version.
  - Linux `.desktop` file and icon.
- Done when: a full session (connect, backup, browse soups, export Names,
  install a package, screenshot, keyboard) works on all three systems
  against Einstein, and on at least one with a real Newton over serial.

### Phase 8: release

- **8.1 Packaging:**
  - macOS: universal `.app` in a `.dmg`, signed and notarized in CI.
  - Windows: `.zip`, plus an installer if wanted (CPack/NSIS or WiX), for
    x64 and arm64.
  - Linux: `.tar.gz` and AppImage (x64, arm64).
- **8.2 Release workflow:** on tag `v*`, build all six, run the tests,
  and attach the artifacts to a GitHub Release.
- **8.3 Switch over:**
  - Update the README.
  - Delete `NCX/` and `NCX.xcodeproj` once nothing is left to look up in
    them, or move them to a `legacy/` branch. The resources (icons, help,
    protocol extensions) will have moved to `resources/` by then.
  - Drop the prebuilt `Newton.framework` from the repo.
  - Have CMake fetch newton-framework from GitHub by tag, with no local
    paths.

---

## 4. Decisions

Settled with Matt on 2026-10-16, except D9.

- **D1. ROM data: C, generated by ResMaker** (revised 2026-10-16; first
  decided: clang-cl for the `.s` files, out of worry that regenerating
  would lose information). Matt asked to check whether ResMaker can make the
  same data as C, and to switch if so. It can: once its templates matched
  forNewton's format, its output was identical to the checked-in files
  (step 0.1b has the details), so the `.s` files were ResMaker's output plus
  two hand edits without data. The C is plain C99, needs no assembler, and
  builds with any compiler. MessagePad's own `.s` files don't matter (Matt:
  forget the MessagePad target).
- **D2. MNP and transports: `newton_dock`** (`MNP.cc`, `NTKTransport.cc`
  from newtdump), extended for Windows. The newtdump code is derived from
  NCX, with many changes to work well with Einstein's PTY hacks, so mixing
  it back into NCX should be easy. It also brings smaller chunk sizes and
  reconnect on failure.
- **D3. Dragging files out of the app:** Export… / Save Screenshot… /
  Copy (images to the clipboard through `Fl_Copy_Surface`), until FLTK
  learns to drag out images and files. Matt plans to add that to FLTK.
- **D4. Rich-text export: RTF with embedded PNGs** (`\pngblip`).
- **D5. Backup file format: the Core Data SQLite schema, unchanged.**
  Existing backups open, and the old macOS NCX can still read new files.
  The two plist blobs in `ZUSERINFO` are left alone on read (the Newton
  sends that data again) and written as before, or left empty.
- **D6. Features dropped:**
  - SyncServices sync and the IOBluetooth endpoint (both dead code;
    Bluetooth serial adapters show up as normal serial ports);
  - Sparkle (already removed), QuickLook, and the dock sound/badge extras.

  Bonjour publishing of the TCP endpoint is optional (macOS `dns_sd` only
  at first).
- **D7. Translators are linked in**, with no runtime plugin loading. No
  plugins beyond NCX's own are known to exist.
- **D8. This branch is pure FLTK.** No Cocoa version is built or
  maintained. `NCX/` and `NCX.xcodeproj` stay only as reference while
  porting. The branch may become a fork.
- **D9. Menu bar with several document windows (open).** Proposal in step
  7.3:
  - macOS: one shared `Fl_Sys_Menu_Bar`;
  - Windows and Linux: an `Fl_Menu_Bar` in every document window, built
    from the same menu table.

  The alternative is one document window at a time (NCX talks to only one
  Newton at a time anyway, through the single `gNCNub`). Then the menu bar
  simply sits in that window, but browsing two backups side by side is
  lost. To be decided before Phase 7.
