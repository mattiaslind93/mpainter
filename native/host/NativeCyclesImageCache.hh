#pragma once
// Declaration mirrored by the narrowly scoped pinned-Blender fork patch 0035.
namespace blender {
struct Image;
struct ImBuf;
// Owner validates session/revision externally. Replaces one immutable CPU cache
// entry without changing GPU storage, partial updates, source or authored dirt.
// Returns an owned old-buffer reference for retirement outside the cache lock.
bool BKE_image_publish_hosted_gpu_buffer(Image *,int tile,ImBuf *,ImBuf **previous);
}
