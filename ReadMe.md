# Newton Connection for Mac OS X (NCX)

A modern replacement for Apple’s classic [Newton Connection Utilities](http://www.unna.org/view.php?/apple/connection_utils/ForMac/NewtonConnectionUtilities) (NCU).

NCX allows you to:

* Backup information from a Newton device to your Mac
* Import and export Dates, Names, Notes, and NewtonWorks documents
* Install packages
* Use your Mac keyboard to enter text on your Newton device

NCX also works with [Einstein](https://github.com/pguyot/Einstein), the Newton 
OS emulation platform.

More information on [NCX](https://newtonresearch.org/connection/) can be found 
on the [Newton Research](https://newtonresearch.org/) website.

## Build Information

The `NCX.xcodeproj` builds NCX as a x86_64-only application using Xcode, 
tested with Xcode 26.3. There are a bunch of back-compatibility warnings, 
but no build errors.

Project dependencies include the following frameworks:

* [Newton.framework](https://github.com/newtonresearch/newton-framework). This 
  provides a NewtonScript environment for data imported from a tethered Newton 
  device. You can use the framework included here, or build your own and link 
  against that. Make an Xcode workspace that includes NCX and the Newton 
  framework for an easier debug life.
  * newton-framework is include at project directory level as Newton.framework,
    compiled as a x86_64 binary for convenience. An Apple Silicon version of 
    the Newton framework is under development as of July 2026 
* [Sparkle](https://github.com/sparkle-project/Sparkle) for automatically 
  updating the app. You should download that separately and link against the 
  framework that project builds.
  * Matt: Sparkle dependency was remove as of July 2026. The integrated version 
    was 1.16.0 from 2016. Updating to 2.9 seemed like a big effort for an 
    application that is rarely updated. It would require maintenance of
    encryption key and a matching web site infrastructure.
* [libical](https://github.com/libical/libical) library for help translating 
  Newton Dates to ical entries. The source is included here; it has been
  modified to work in an ARC world.

## Roadmap 2026

Matt: Hey friends! Just a quick update on NCX and the Newton Framework. Since
NCX is built heavily on the Newton Framework — which, at the moment, only runs 
on Intel Macs — we're facing a bit of a deadline: Apple is ending Intel support
in fall 2027, and macOS Tahoe is already warning about it.

So, my main goal for 2026 is to port the Newton Framework to pure C++, so it 
can be compiled on any 32- or 64-bit CPU, not just Intel. Once that’s done, 
I’ll turn to porting NCX as well. With any luck, this should help us avoid 
any disruption in macOS support as the platform moves forward!

After that, I don’t plan to add any new features or major upgrades. I’ll 
just be around to fix any moderate or serious bugs that come up — so ongoing 
support will be pretty minimal.

