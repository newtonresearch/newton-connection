/*
 File: Extensions.cc

 The protocol extensions built into NCX (Extensions.h).
 */

#include "dock/Extensions.h"

namespace ncx {

std::vector<uint8_t> Extension(EventType inId)
{
  for (const EmbeddedExtension * e = kEmbeddedExtensions; e->id; ++e) {
    EventType id = (EventType)((uint32_t)(uint8_t)e->id[0] << 24 | (uint32_t)(uint8_t)e->id[1] << 16
                               | (uint32_t)(uint8_t)e->id[2] << 8 | (uint8_t)e->id[3]);
    if (id != inId)
      continue;
    std::vector<uint8_t> bytes(e->id, e->id + 4);
    bytes.insert(bytes.end(), e->nsof, e->nsof + e->size);
    return bytes;
  }
  return std::vector<uint8_t>();
}

} // namespace ncx
