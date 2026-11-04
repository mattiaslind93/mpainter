#pragma once
#include "TextureDocument.hh"
#include <atomic>
namespace bts {
struct FlattenImage {
 int width=0,height=0;
 std::shared_ptr<std::vector<FillTexture::Pixel>> pixels;
};
struct FlattenedLayer {
 QUuid source;
 TextureLayer layer;
 std::array<std::map<int,FlattenImage>,15> images;
};
struct FlattenPlan {
 FlattenedLayer result;
 std::array<bool,15> enabled{},contextual{};
 int mask_canvas=0;
 std::uint64_t snapshot_bytes=0; // CPU export/oracle buffers; GPU authoring streams chunks.
};
// Returns an isolated prefix grouped for the existing GPU capture bridge.
TextureSet precompose_capture_source(const TextureSet &,int below);
void install_precompose_layer(TextureSet &,TextureLayer,int index,bool update=false);
std::uint64_t flatten_available_ram_bytes();
FlattenPlan prepare_flatten_layer(const TextureSet &,const QUuid &);
// Immutable input, CPU pixels only. No native IDs, files, assets or UI mutation.
// Cancellation returns no result and never disables the original group.
std::optional<FlattenedLayer> flatten_layer_pixels(const TextureSet &,const QUuid &,const std::atomic_bool &cancel);
void install_flattened_layer(TextureSet &,TextureLayer,const QUuid &source);
}
