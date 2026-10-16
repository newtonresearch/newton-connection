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
| CoreGraphics (`CGContext`) | drawing ink, shapes, bitmaps (through Newton.framework `InitDrawing`/`DrawBitmap`/…) | a drawing back end interface; FLTK `Fl_Image_Surface` implementation (step 0.5) |
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
  `DrawPolygon`, `DrawPicture`, `InkDrawInRect`, and rectangle helpers;
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
   and Windows need another back end.
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
  translators, and has **no FLTK dependency**. Both `ncx-cli` and the GUI
  link it, so almost everything can be tested without a display.
- **`newton_ncx`** (static library in newton-framework) is the object
  system, NSOF, pipes, packages, compression, ROM data, and the drawing
  core with a back-end interface.
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
- **0.5 Drawing back-end interface.**
  - Make `QDDrawing.cc` call a small `QDDevice` interface (stroke/fill
    path, rect, oval, polygon, bitmap, pattern, pen) instead of Quartz.
  - Move the Quartz version behind it (macOS only, keeps `newtc`
    working).
  - NCX provides an FLTK version (`Fl_Image_Surface`) later, in step 6.3.
  - `InitDrawing(CGContextRef, int)` becomes `InitDrawing(QDDevice*, int)`.
  - Done when: `newtc` draws as before, and `newton_ncx` builds without
    CoreGraphics.
- **0.6 Move the Dock link into a shared target.**
  - `MNP.cc`, `NTKTransport.cc`, `DES.c` (and maybe `DockLink`) become a
    small `newton_dock` library used by `newtdump` and NCX.
  - Done when: `newtdump` and its tests still pass.

### Phase 1: NCX build skeleton and CI

- **1.1 Top-level `CMakeLists.txt`.**
  - C++17.
  - `FetchContent` for FLTK (pinned `release-1.4.5`), newton-framework
    (pinned commit), and the SQLite amalgamation.
  - `-DFETCHCONTENT_SOURCE_DIR_FLTK=~/dev/fltk-1.4.git` and
    `…_NEWTON=~/dev/newton-framework.matt.git` for local work.
  - FLTK as static libs, with Fluid built (needed for `.fl` files).
  - Done when: `cmake -B build && cmake --build build` builds an empty
    FLTK window that links `newton_ncx` and prints an NSOF round trip.
- **1.2 Source layout.** Create `src/…` as in section 2. `NCX/` and
  `NCX.xcodeproj` stay for reference only; this branch builds pure FLTK
  (D8).
- **1.3 GitHub Actions: build.**
  - Matrix: `ubuntu-24.04` (x64), `ubuntu-24.04-arm`, `windows-2022`
    (x64), `windows-11-arm`, `macos-15` (universal2:
    `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`).
  - Windows jobs use MSVC (`-A x64`, `-A ARM64`), or the ClangCL toolset
    if 0.4 needed it.
  - Upload build artifacts.
  - Check the runner labels when writing the workflow.
  - Done when: all five jobs are green.
- **1.4 GitHub Actions: tests.**
  - `ctest` on every job.
  - The x86_64 macOS slice runs under Rosetta (`arch -x86_64`).
  - Done when: the NSOF test runs everywhere.

### Phase 2: base layer (replacing Foundation)

- **2.1 Strings:**
  - UTF-8 `std::string` throughout.
  - Ref ↔ UTF-8 helpers (replace `MakeNSString`, `MakeString…`).
  - A MacRoman table (the code uses `NSMacOSRomanStringEncoding` 7
    times).
  - Ports of the `NSString-Extensions` that are actually used.
- **2.2 Dates:**
  - NewtonTime (minutes since 1904) ↔ `time_t` ↔ Core Data time (seconds
    since 2001).
  - Formatting for file names and display (replaces `NSDateFormatter`
    uses).
- **2.3 Paths and app folders (`platform/`):**
  - macOS keeps `~/Library/Application Support/Newton Connection`, so
    existing files are found.
  - Windows: `%APPDATA%\Newton Connection`.
  - Linux: `$XDG_DATA_HOME/newton-connection`.
- **2.4 Preferences:** `Fl_Preferences`, same key names as
  `PreferenceKeys.h`.
- **2.5 Logging:** `REPprintf`/`NSLog` → one logger with levels and an
  optional log file (prefs `LogToFile`, `LogLevel`).
- **2.6 Threads:**
  - `SerialQueue` (a thread with a task queue), `Semaphore`, `Timer` (for
    the tickler).
  - `Progress` with a cancel flag.
  - `Observers` (replacing the six `kDock…Notification`s), delivered on
    the UI thread through a hook the UI installs.
- Done when (all of 2.x): unit tests for strings, dates and queues pass in
  CI.

### Phase 3: comms layer (no GUI)

- **3.1 Transports.**
  - Build on `newton_dock`'s `NTKTransport` (tcp server for Einstein and
    the Newton's network option, serial, Einstein PTY), plus the Einstein
    FIFO endpoint (POSIX).
  - Add Windows serial (Win32, overlapped) and Winsock TCP.
  - Use one reader thread per transport, so Windows serial needs no
    `select()`.
- **3.2 Listening on all transports at once.** Port
  `NCEndpointController`: listen on every enabled transport, keep the one
  that connects, close the others.
- **3.3 Serial port list (`platform/`):** IOKit (macOS), `/dev`
  scanning (Linux), `SERIALCOMM` registry (Windows), for the preferences
  panel.
- **3.4 MNP.** Use `newton_dock`'s `MNP.cc` (D2). It started as NCX's
  `MNPSerialEndpoint` and was changed to work well with Einstein's PTY
  hacks, so merging is mostly mixing the newtdump code back in.
  - Diff it against `MNPSerialEndpoint` and carry over anything NCX does
    that newtdump dropped.
  - Take newtdump's smaller chunk sizes and its reconnect after a failed
    connection. Reconnect is optional per operation (useful for long
    backups and ROM dumps).
- **3.5 Dock events and session.**
  - Port `NCBuffer`, `CRC`, `DockEvent`, `DockEventQueue`, `Session`,
    `Cursor` to C++.
  - `NCDockEvent` becomes a struct with command, length, data and a lazily
    unflattened Ref.
- **3.6 `ncx-cli` v0:** `ncx-cli --target tcp info` connects, does the
  handshake and password, and prints the Newton's name, ROM version and
  stores.
- **3.7 Fake Newton for NCX.**
  - Extend `newtdump-test-newton` (or a copy in `tests/`) to answer
    NCX's handshake, store/soup queries and cursor calls from a fixture.
  - Done when: a CI test runs `ncx-cli info` against it on all
    platforms, and `ncx-cli info` works against Einstein locally.

### Phase 4: data model and persistence (no GUI)

- **4.1 Model classes:**
  - Plain C++ `Device`, `UserInfo`, `Store`, `Soup`, `App`, `Entry`,
    `RestoreInfo`.
  - `Entry` keeps its NSOF blob and unflattens on demand.
  - The UI-only properties (`image`, `icon`, `status`) move to `ui/`.
- **4.2 SQLite store:**
  - Read and write the Core Data schema from 1.5, including `Z_ENT`,
    `Z_OPT` and `Z_PRIMARYKEY` upkeep, and keep `Z_METADATA` as found
    (decision D5).
  - Transactions replace `save:`; the in-thread contexts in
    `makeManagedObjectContextForThread` go away.
- **4.3 Device chooser and legacy backups:**
  - List the `.newtondevice` files and their device names (the
    `docList`/`docSheet` logic).
  - Port `BackupDocument` (importing old backup files).
- **4.4 Tests:**
  - Load a fixture `.newtondevice` made with the current macOS NCX from
    Einstein (not personal data).
  - Check the entry counts and that entries unflatten.
  - Write it back, then reopen it.
  - Done when: these pass in CI, and the macOS Core Data NCX still opens a
    file written by the new code (checked by hand once).

### Phase 5: dock controller and components (no GUI)

- **5.1 `DockController`:**
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
  2. ROM dump (`ncx-cli rom out.bin`). Take the approach from newtdump
     (block size, resume after a reconnect).
  3. package install (`ncx-cli install a.pkg b.pkg`), with `PkgPart`
  4. backup, full and selective (`ncx-cli backup`), which writes the
     `.newtondevice`
  5. restore
  6. import of entries and soup modification (delete entries or packages)
  7. keyboard passthrough (an API only; `ncx-cli type "text"`)
  8. the Newton 1 session (`Newton1Component`)
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
  - The FLTK `QDDevice` (`Fl_Image_Surface`) from step 0.5.
  - Ink decompression and drawing (`InkDrawInRect`), shapes, bitmaps.
  - Export as PNG (`fl_write_png`).
  - Note: `Fl_Image_Surface` needs a display on Linux, so CI uses
    `xvfb-run` for these tests. Alternatively, add a pure software
    `QDDevice` later.
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
