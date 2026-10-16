/*
 File: DockRefs.cc

 NSOF in Docking Protocol events (DockRefs.h).
 */

#include "comms/DockRefs.h"
#include "base/Log.h"

#include "Pipes.h"

void FlattenRef(RefArg inRef, CPipe & inPipe);
long FlattenRefSize(RefArg inRef);
Ref UnflattenRef(CPipe & inPipe);

namespace ncx {

std::vector<uint8_t> FlattenToBytes(RefArg inObj)
{
  long size = FlattenRefSize(inObj);
  std::vector<uint8_t> bytes((size_t)size);
  CPtrPipe pipe;
  pipe.init(bytes.data(), (size_t)size, false, nullptr);
  FlattenRef(inObj, pipe);
  return bytes;
}


Ref UnflattenBytes(const std::vector<uint8_t> & inBytes, size_t inOffset, size_t * outEnd)
{
  if (outEnd)
    *outEnd = inOffset;
  if (inOffset >= inBytes.size())
    return NILREF;
  RefVar obj;
  newton_try
  {
    CPtrPipe pipe;
    pipe.init((void *)(inBytes.data() + inOffset), inBytes.size() - inOffset, false, nullptr);
    obj = UnflattenRef(pipe);
    if (outEnd)
      *outEnd = inOffset + (size_t)pipe.readPosition();
  }
  newton_catch_all
  {
    obj = NILREF;
    Log(kLogMinimum, "NSOF: can't read an object (%zu bytes at %zu)", inBytes.size() - inOffset, inOffset);
  }
  end_try;
  return obj;
}


DockEvent MakeRefEvent(EventType inCommand, RefArg inObj)
{
  return DockEvent::MakeData(inCommand, FlattenToBytes(inObj));
}


Ref EventRef(const DockEvent & inEvent)
{
  return UnflattenBytes(inEvent.data);
}


Ref EventRef1(const DockEvent & inEvent)
{
  return UnflattenBytes(inEvent.data, 4);
}


Ref EventRef2(const DockEvent & inEvent)
{
  size_t end = 0;
  UnflattenBytes(inEvent.data, 0, &end);
  return end > 0 ? UnflattenBytes(inEvent.data, end) : (Ref)NILREF;
}

} // namespace ncx
