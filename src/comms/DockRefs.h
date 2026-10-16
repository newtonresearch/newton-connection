/*
 File: DockRefs.h

 Newton objects in Docking Protocol events: NSOF out of an event's data
 (NCDockEvent's ref, ref1, ref2) and into it. Includes newton-framework's
 headers: for the dock's code (the dock thread), not the UI.
 */

#ifndef NCX_COMMS_DOCKREFS_H
#define NCX_COMMS_DOCKREFS_H

#include "comms/DockEvent.h"

#include "Objects.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ncx {

/** An object as NSOF. */
std::vector<uint8_t> FlattenToBytes(RefArg inObj);

/** The object (NSOF) at inOffset of inBytes; outEnd, if given, where it
    ends. NIL if the bytes aren't NSOF (and that is logged). */
Ref UnflattenBytes(const std::vector<uint8_t> & inBytes, size_t inOffset = 0, size_t * outEnd = nullptr);

/** An event with an object as its data. */
DockEvent MakeRefEvent(EventType inCommand, RefArg inObj);

/** The event's object: its data (ref), after a 32-bit value (ref1), the
    second of two objects (ref2). */
Ref EventRef(const DockEvent & inEvent);
Ref EventRef1(const DockEvent & inEvent);
Ref EventRef2(const DockEvent & inEvent);

} // namespace ncx

#endif // NCX_COMMS_DOCKREFS_H
