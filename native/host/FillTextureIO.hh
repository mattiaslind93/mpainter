#pragma once
#include "AssetCatalog.hh"
#include "TextureDocument.hh"
#include <QStringList>
#include <atomic>
#include <functional>
namespace bts {
struct TextureDecodeProgress {
  int tile = 0, index = 0, total = 0;
  QString filename, phase, input_space;
  double read_ms = 0, convert_ms = 0;
  std::uint64_t decoded_bytes = 0;
};
struct TextureDecodeControl {
  const std::atomic_bool *cancel = nullptr;
  std::function<void(const TextureDecodeProgress &)> progress;
  bool verify_files = true;
};
std::uint64_t texture_decode_available_bytes();
std::shared_ptr<const FillTexture> decode_fill_texture(const StudioAsset &,
                                                       bool raw = false,
                                                       const std::string &source_space = {},
                                                       const TextureDecodeControl &control = {});
QStringList fill_texture_color_spaces();
void resolve_fill_textures(TextureDocument &, const std::vector<StudioAsset> &);
void resolve_material_asset_textures(std::vector<StudioAsset> &);
}  // namespace bts
