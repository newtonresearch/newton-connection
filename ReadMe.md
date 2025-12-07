# Newton Connection for Mac OS X (NCX)

A modern replacement for Apple’s classic [Newton Connection Utilities](http://www.unna.org/view.php?/apple/connection_utils/ForMac/NewtonConnectionUtilities) (NCU).

NCX allows you to:

* Backup information from a Newton device to your Mac
* Import and export Dates, Names, Notes, and NewtonWorks documents
* Install packages
* Use your Mac keyboard to enter text on your Newton device

NCX also works with [Einstein](https://github.com/pguyot/Einstein), the Newton OS emulation platform.

More information on [NCX](https://newtonresearch.org/connection/) can be found on the [Newton Research](https://newtonresearch.org/) website.

## Build Information

The `NCX.xcodeproj` builds the NCX application using Xcode 8.

Project dependencies include the following frameworks:

* [Newton.framework](https://github.com/newtonresearch/newton-framework). This provides a NewtonScript environment for data imported from a tethered Newton device. You can use the framework included here, or build your own and link against that. Make an Xcode workspace that includes NCX and the Newton framework for an easier debug life.
* [Sparkle](https://github.com/sparkle-project/Sparkle) for automatically updating the app. You should download that separately and link against the framework that project builds.
* [libical](https://github.com/libical/libical) library for help translating Newton Dates to ical entries. The source is included here; it has been modified to work in an ARC world.
