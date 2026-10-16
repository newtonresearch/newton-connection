# Protocol extensions

Functions NCX sends to the Newton during a dock session (the Docking
Protocol's `kDRegProtocolExtension`, `pext`), so that a Newton needs no
package for what NCX asks of it. Each `<id>.nsof` is one function as NSOF;
its file name is the 4-letter command that calls it. The build embeds all of
them into NCX (`cmake/EmbedExtensions.cmake`, read by `src/dock/Extensions.h`).

| Extension | What it does | Where it comes from |
|---|---|---|
| `NCss` | screenshot: takes the screen with the Toolkit app's `ScreenShotFn` a few rows at a time (white ones not sent) | `NCss.ns` |
| `scrn` | hides the Dock slip while the user arranges the screen for a screenshot (and shows it again) | the Cocoa NCX's `NCX/NTK/scrn.stream`, without its 4-byte id |
| `NDmp` | reads physical memory (the ROM, PCMCIA cards) a block at a time | newtdump's, newton-framework's `Matt/NewtDump/Newton/Extension.ns` (and its ARM natives, `PhysNatives.ns`); the same bytes newtdump sends |

## Changing one

The `.ns` files are the source. newtc (newton-framework's, built with
`NEWTON_BUILD_TOOLS`) compiles them; the `.nsof` files are checked in, so a
build needs no newtc:

```
cmake -B build -DNCX_NEWTC=<path to newtc>
cmake --build build --target ncx_extensions
```

newtdump's extension is compiled from the fetched newton-framework (its
source stays there).

Look at what came out with `newtc -nsof <id>.nsof -decompile`. The
extensions taken over from the Cocoa NCX have no source; decompiling them
shows what they do.
