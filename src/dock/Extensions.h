/*
 File: Extensions.h

 The protocol extensions NCX sends to a Newton (PORTING.md 5.1): functions
 the Newton runs for NCX during a dock session, installed with
 kDRegProtocolExtension. They are built into NCX (src/dock/extensions,
 cmake/EmbedExtensions.cmake), where the Cocoa app read .stream files from
 its bundle.
 */

#ifndef NCX_DOCK_EXTENSIONS_H
#define NCX_DOCK_EXTENSIONS_H

#include "comms/DockProtocol.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ncx {

struct EmbeddedExtension
{
  const char * id;                  // its command, "NCss"
  const unsigned char * nsof;       // the function
  size_t size;
};

/** The extensions built in, the last one { nullptr, nullptr, 0 }. */
extern const EmbeddedExtension kEmbeddedExtensions[];

/** An extension as Session::loadExtension sends it: its command (4 bytes,
    big-endian) and the function (NSOF). Empty if there's none of that id. */
std::vector<uint8_t> Extension(EventType inId);

} // namespace ncx

#endif // NCX_DOCK_EXTENSIONS_H
