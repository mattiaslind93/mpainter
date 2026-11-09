#pragma once
#include "ThumbnailMetadata.hh"
namespace bts {
// get() may retain the preceding completed packet while a newer fence is in
// flight. Readiness belongs to this request, not merely to a nonempty cache.
inline bool smart_mask_thumbnail_packet_ready(bool pending,
                                              const ThumbnailMetadata *packet,
                                              std::uint64_t ticket)
{
  return !pending && packet && packet->ticket == ticket;
}
}  // namespace bts
