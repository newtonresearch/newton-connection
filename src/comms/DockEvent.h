/*
 File: DockEvent.h

 A Docking Protocol event (the Cocoa app's NCDockEvent, PORTING.md 3.5): a
 command, the length as it goes over the wire, and the data. Events are
 encoded with encode() and found in a byte stream by DockEventParser. No
 framework types here: NSOF in the data is DockRefs.h's.
 */

#ifndef NCX_COMMS_DOCKEVENT_H
#define NCX_COMMS_DOCKEVENT_H

#include "comms/DockProtocol.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ncx {

struct DockEvent
{
  EventType command = 0;
  /** The length field: the data's length, kIndeterminateLength, or what the
      command says it is (some count characters). */
  uint32_t length = 0;
  /** The data, without the padding. */
  std::vector<uint8_t> data;

  DockEvent() { }
  explicit DockEvent(EventType inCommand) : command(inCommand) { }

  /** A command alone, with a 32-bit value, with data (the length field
      that of the data unless given). */
  static DockEvent Make(EventType inCommand);
  static DockEvent MakeValue(EventType inCommand, int32_t inValue);
  static DockEvent MakeData(EventType inCommand, const void * inData, size_t inSize);
  static DockEvent MakeData(EventType inCommand, std::vector<uint8_t> inData);
  static DockEvent MakeData(EventType inCommand, uint32_t inLength, std::vector<uint8_t> inData);

  /** A 32-bit number in the data (big-endian); 0 if the data is shorter. */
  uint32_t longAt(size_t inOffset) const;
  /** The first 32 bits as a signed number: an error code, a value. */
  int32_t value() const { return (int32_t)longAt(0); }

  /** The bytes on the wire: 'newt' 'dock', command, length, data, padding
      to 4 bytes (none after an event of no set length). */
  std::vector<uint8_t> encode() const;

  /** For the log: "dres [4] -28012", "name [96] 00 00 00 40 ...". */
  std::string describe() const;
};

/** Finds the events in a byte stream that comes in any pieces. */
class DockEventParser
{
public:
  /** Bytes in; the events completed by them are appended to outEvents. An
      event of kIndeterminateLength ends where the next 'newt' 'dock' starts. */
  void feed(const uint8_t * inData, size_t inSize, std::vector<DockEvent> & outEvents);
  void reset();

  /** In an event of kIndeterminateLength: its command and its data so far
      (the Newton knows where such an event ends by reading the NSOF in it;
      the fake Newton does the same). */
  bool openEnded(EventType & outCommand, const std::vector<uint8_t> * & outData) const;
  /** End it after inLength bytes of data (outEvents gets it); what came
      after is dropped. */
  void endOpenEnded(size_t inLength, std::vector<DockEvent> & outEvents);

private:
  enum State { kSignature, kCommand, kLength, kData, kOpenEnded };
  State mState = kSignature;
  size_t mCount = 0;           // bytes of the signature, command, length, or data so far
  size_t mPadded = 0;          // the data with its padding
  DockEvent mEvent;
};

} // namespace ncx

#endif // NCX_COMMS_DOCKEVENT_H
