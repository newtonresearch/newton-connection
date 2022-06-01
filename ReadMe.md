NCX -- Newton Connection for Mac OS X 
====
A modern replacement for Apple’s Newton Connection Utilities (NCU).

NCX allows you to backup information from a Newton device to your Mac desktop, import and export Dates, Names, Notes and NewtonWorks, install packages and use your Mac desktop keyboard to enter text on your Newton device.

It works with the [Einstein emulator](https://github.com/pguyot/Einstein) too!


BUILD INFO
----
The NCX.xcodeproj builds the NCX app. Here at the Newton Research labs we used Xcode 8.
The project depends on these frameworks:

* [Newton.framework](https://github.com/newtonresearch/newton-framework). This provides a NewtonScript environment for data imported from a tethered Newton device. You can use the framework included here, or build your own and link against that. Make an Xcode workspace that includes NCX and the Newton framework for an easier debug life.
* [Sparkle](https://github.com/sparkle-project/Sparkle) for automatically updating the app. You should download that separately and link against the framework that project builds.
* [libical](https://github.com/libical/libical) library for help translating Newton Dates to ical entries. The source is included here; it has been modified to work in an ARC world.
