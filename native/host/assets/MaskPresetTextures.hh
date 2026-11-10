// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceAvailability.hh"
#pragma once
#include "LayerMask.hh"
#include "FillTextureJSON.hh"
#include "FillTexturePool.hh"
#include <QDataStream>
#include <QJsonDocument>
#include <map>
namespace bts {
// Presets retain decoded float pixels so reuse never depends on an artist's
// original path, OCIO installation, or a second lossy image decode.
struct MaskPresetTextures {
  static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559 &&
                sizeof(FillTexture::Pixel) == 16);
  std::vector<std::shared_ptr<const FillTexture>> resources;
  QJsonArray descriptors, bindings;
  explicit MaskPresetTextures(const LayerMask &mask)
  {
    std::map<std::string, int> shared;
    for (const auto &effect : mask_effects_recursive(mask.effects)) {
      const auto &texture = effect.mapping.projection.texture;
      if (!texture)
        continue;
      texture->validate();
      if (effect.kind != MaskEffectKind::Fill)
        throw std::invalid_argument("Only Fill effects accept a smart-mask texture");
      std::string key = texture->hash + ":" + std::to_string(texture->raw) + ":" +
                        texture->source_space + ":" + std::to_string(texture->width) + ":" +
                        std::to_string(texture->height);
      if (texture->tiles)
        for (const auto &[tile, member] : *texture->tiles)
          key += ":" + std::to_string(tile) + ":" + member->hash;
      auto [found, fresh] = shared.try_emplace(key, int(resources.size()));
      if (fresh) {
        resources.push_back(texture);
        auto descriptor = fill_texture_json(texture).toObject();
        descriptor["runtime_input_space"] = QString::fromStdString(texture->input_space);
        if (texture->tiles) {
          auto tiles = descriptor["tiles"].toArray();
          for (qsizetype i = 0; i < tiles.size(); ++i) {
            auto item = tiles[i].toObject();
            auto image = item["image"].toObject();
            image["runtime_input_space"] = QString::fromStdString(
                texture->tiles->at(item["tile"].toInt())->input_space);
            item["image"] = image;
            tiles[i] = item;
          }
          descriptor["tiles"] = tiles;
        }
        descriptors.append(descriptor);
      }
      bindings.append(QJsonObject{{"effect", effect.id.toString(QUuid::WithoutBraces)},
                                  {"resource", found->second}});
    }
  }
  void write(QDataStream &stream, std::uint64_t &decoded, std::uint64_t budget) const
  {
    const auto plane = [&](const FillTexture &texture) {
      texture.validate();
      const auto count = std::uint64_t(texture.width) * texture.height;
      const auto text_bytes = resource_product(texture.input_space.size(), 16);
      decoded = resource_sum(decoded, text_bytes);
      if (!texture.pixels || texture.pixels->size() != count)
        throw std::length_error("Mask bitmap dimensions do not match its pixels");
      decoded += count * sizeof(FillTexture::Pixel);
      const auto shared = intern_fill_texture_pixels(texture, texture.pixels);
      for (const auto &pixel : *shared)
        for (float value : pixel)
          if (!std::isfinite(value))
            throw std::invalid_argument("Non-finite smart-mask bitmap");
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
      const char *data = reinterpret_cast<const char *>(shared->data());
      std::uint64_t remaining = count * sizeof(FillTexture::Pixel);
      while (remaining) {
        const int size = int(std::min<std::uint64_t>(remaining, 1024 * 1024));
        if (stream.writeRawData(data, size) != size)
          throw std::runtime_error("Cannot write smart-mask bitmap");
        data += size;
        remaining -= size;
      }
#else
      for (const auto &pixel : *shared)
        for (float value : pixel)
          stream << value;
#endif
    };
    for (const auto &texture : resources) {
      if (texture->tiles) {
        for (const auto &[tile, member] : *texture->tiles)
          plane(*member);
      }
      else
        plane(*texture);
    }
  }
  static void read(QDataStream &stream,
                   LayerMask &mask,
                   const QJsonObject &root,
                   std::uint64_t &decoded,
                   std::uint64_t budget,
                   std::size_t maximum_bindings = std::numeric_limits<std::size_t>::max())
  {
    const auto fail = []() { throw std::invalid_argument("Invalid smart-mask texture archive"); };
    if (!root["texture_resources"].isArray() || !root["texture_bindings"].isArray() ||
        std::size_t(root["texture_resources"].toArray().size()) > maximum_bindings ||
        std::size_t(root["texture_bindings"].toArray().size()) > maximum_bindings)
      fail();
    const auto plane = [&](const FillTexture &descriptor, const QJsonValue &runtime_space) {
      if (!runtime_space.isString() || runtime_space.toString().contains(QChar::Null))
        fail();
      const auto text_bytes = resource_product(runtime_space.toString().size(), 16);
      if (decoded > budget || text_bytes > budget - decoded)
        fail();
      decoded = resource_sum(decoded, text_bytes);
      auto texture = std::make_shared<FillTexture>(descriptor);
      const auto count = std::uint64_t(texture->width) * texture->height;
      if (decoded > budget)
        fail();
      decoded += count * sizeof(FillTexture::Pixel);
      auto pixels = std::make_shared<std::vector<FillTexture::Pixel>>(std::size_t(count));
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
      char *data = reinterpret_cast<char *>(pixels->data());
      std::uint64_t remaining = count * sizeof(FillTexture::Pixel);
      while (remaining) {
        const int size = int(std::min<std::uint64_t>(remaining, 1024 * 1024));
        if (stream.readRawData(data, size) != size)
          fail();
        data += size;
        remaining -= size;
      }
#else
      for (auto &pixel : *pixels)
        for (float &value : pixel)
          stream >> value;
#endif
      for (const auto &pixel : *pixels)
        for (float value : pixel)
          if (!std::isfinite(value))
            fail();
      if (stream.status() != QDataStream::Ok)
        fail();
      texture->input_space = runtime_space.toString().toStdString();
      if (texture->input_space.empty() && texture->raw)
        texture->input_space = "Non-Color/raw";
      texture->pixels = intern_fill_texture_pixels(*texture, std::move(pixels));
      texture->validate();
      return texture;
    };
    std::vector<std::shared_ptr<const FillTexture>> resources;
    for (const auto &entry : root["texture_resources"].toArray()) {
      auto descriptor = read_fill_texture_json(entry);
      if (!descriptor)
        fail();
      std::shared_ptr<FillTexture> texture;
      if (descriptor->tiles) {
        texture = std::make_shared<FillTexture>(*descriptor);
        auto tiles = std::make_shared<std::map<int, std::shared_ptr<const FillTexture>>>();
        for (const auto &item : entry.toObject()["tiles"].toArray()) {
          const auto metadata = item.toObject();
          const int tile = metadata["tile"].toInt();
          tiles->emplace(tile,
                         plane(*descriptor->tiles->at(tile),
                               metadata["image"].toObject()["runtime_input_space"]));
        }
        texture->tiles = std::move(tiles);
        texture->input_space = entry.toObject()["runtime_input_space"].toString().toStdString();
        texture->pixels = texture->tiles->begin()->second->pixels;
        texture->validate();
      }
      else
        texture = plane(*descriptor, entry.toObject()["runtime_input_space"]);
      resources.push_back(std::move(texture));
    }
    std::set<QUuid> used;
    for (const auto &entry : root["texture_bindings"].toArray()) {
      if (!entry.isObject())
        fail();
      const auto binding = entry.toObject();
      const QUuid id(binding["effect"].toString());
      const int resource = binding["resource"].toInt(-1);
      if (id.isNull() || !used.insert(id).second || !binding["resource"].isDouble() ||
          binding["resource"].toDouble() != resource || resource < 0 ||
          resource >= int(resources.size()))
        fail();
      MaskEffect *effect = nullptr;
      for (auto &candidate : mask_effects_recursive(mask.effects))
        if (candidate.id == id) {
          if (effect)
            fail();
          effect = &candidate;
        }
      if (!effect || effect->kind != MaskEffectKind::Fill)
        fail();
      effect->mapping.projection.texture = resources[resource];
    }
  }
};
}  // namespace bts
