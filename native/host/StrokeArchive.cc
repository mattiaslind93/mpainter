#include "TextureExtent.hh"
#include "ResourceAvailability.hh"
#include "StrokeArchive.hh"
#include "TextureMemory.hh"
#include "HistoryArchiveCodec.hh"
#include "EffectPaintIO.hh"
#include <string>
#include <tuple>
#include <QDataStream>
#include <QIODevice>
#include <QJsonArray>
#include <QtEndian>
namespace bts {
namespace {
[[noreturn]] void fail()
{
  throw std::invalid_argument("Invalid spatial stroke archive");
}
void write_raster(QDataStream &stream, const StrokeRaster &raster)
{
  for (const auto *tiles : {&raster.paint, &raster.ao}) {
    stream << quint32(tiles->tiles().size());
    for (const auto &[tile, pixels] : tiles->tiles()) {
      quint32 pages = 0;
      pixels.visit_pages(
          [&](int, std::uint32_t, std::span<const PaintPixels::Pixel>) { ++pages; });
      stream << qint32(tile) << quint32(pixels.resolution()) << pages;
      pixels.visit_pages(
          [&](int channel, std::uint32_t key, std::span<const PaintPixels::Pixel> values) {
            stream << quint8(channel) << quint32(key);
            for (const auto &pixel : values)
              for (float value : pixel)
                stream << value;
          });
    }
  }
}
StrokeRaster read_raster(QDataStream &stream,
                         int resolution,
                         const std::set<int> &allowed,
                         std::uint64_t &decoded,
                         std::uint64_t budget)
{
  StrokeRaster result;
  for (auto *tiles : {&result.paint, &result.ao}) {
    quint32 tile_count;
    stream >> tile_count;
    if (tile_count > allowed.size())
      fail();
    std::set<int> seen;
    for (quint32 t = 0; t < tile_count; ++t) {
      qint32 tile;
      quint32 size, pages;
      stream >> tile >> size >> pages;
      if (!allowed.contains(tile) || !seen.insert(tile).second || size != quint32(resolution) ||
          std::uint64_t(pages) > resource_product(5, sparse_texture_page_count(resolution)))
        fail();
      decoded += std::uint64_t(pages) * PaintPixels::page_side * PaintPixels::page_side * 16;
      if (decoded > budget)
        fail();
      for (quint32 p = 0; p < pages; ++p) {
        quint8 channel;
        quint32 key;
        stream >> channel >> key;
        if (channel > 4 || (tiles == &result.ao && channel != 2))
          fail();
        std::array<PaintPixels::Pixel, PaintPixels::page_side * PaintPixels::page_side> values;
        for (auto &pixel : values)
          for (auto &value : pixel)
            stream >> value;
        if (stream.status() != QDataStream::Ok)
          fail();
        tiles->assign_page(resolution, channel, key, values, tile);
      }
    }
  }
  return result;
}
struct HistoryData {
  std::shared_ptr<const StrokeHistory> history;
  StrokeRaster flat;
  int resolution;
};
HistoryData history_data(const TextureSet &set, const TextureLayer &layer, int role)
{
  if (role == 0)
    return {layer.strokes,
            {layer.pixels, layer.ao_pixels},
            TextureDocument::authoring_resolution(set, layer)};
  if (role == 1) {
    if (!layer.mask)
      return {};
    return {layer.mask->strokes,
            {layer.mask->pixels, {}},
            TextureDocument::authoring_resolution(set, layer, true)};
  }
  const auto &user = role == 2 + custom_channel_count ? layer.displacement :
                                                        layer.users.at(role - 2);
  return {
      user.strokes, {user.pixels, {}}, custom_authoring_resolution(user, set.texture_resolution)};
}
std::shared_ptr<const StrokeHistory> &history_reference(TextureLayer &layer, int role)
{
  if (role == 0)
    return layer.strokes;
  if (role == 1) {
    if (!layer.mask)
      fail();
    return layer.mask->strokes;
  }
  return role == 2 + custom_channel_count ? layer.displacement.strokes :
                                            layer.users.at(role - 2).strokes;
}

template<class Paint> auto &effect_history(Paint &paint, int role)
{
  if (role == 0)
    return paint.strokes;
  if (role == 9)
    return paint.displacement_strokes;
  return paint.user_strokes.at(role - 1);
}
StrokeRaster effect_flat(const EffectPaint &paint, int role)
{
  if (role == 0)
    return {paint.pixels, paint.ao};
  if (role == 9)
    return {paint.displacement, {}};
  return {paint.users.at(role - 1), {}};
}

}  // namespace
bool limit_stroke_history(TextureDocument &, std::optional<std::uint64_t> maximum)
{
  // Recipes and COW snapshots describe overlapping/virtual GPU pages. Summing
  // their page counts charged the same data repeatedly and deleted histories.
  // The shared page pool accounts actual unique payloads; its stable mapped
  // storage can be flushed/evicted without losing operations or rasterizing.
  auto available = texture_available_ram_bytes();

  const auto allocated = allocated_history_bytes();
  const auto budget = maximum.value_or(resource_sum(allocated, available));
  return request_history_spill(budget);
}
QByteArray write_stroke_archive(const TextureDocument &document)
{
  const auto budget = history_archive::decoded_budget();
  HistoryArchiveWriter writer;
  QDataStream stream(&writer);
  stream.setVersion(QDataStream::Qt_6_8);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
  quint32 entries = 0;
  for (const auto &set : document.texture_sets())
    for (const auto &layer : set.layers)
      for (int role = 0; role < 3 + custom_channel_count; ++role) {
        const auto data = history_data(set, layer, role);
        entries += !stroke_history_json(data.history, data.flat).isNull();
      }
  stream << quint32(0x42545331) << entries;
  for (const auto &set : document.texture_sets())
    for (const auto &layer : set.layers)
      for (int role = 0; role < 3 + custom_channel_count; ++role) {
        const auto data = history_data(set, layer, role);
        if (stroke_history_json(data.history, data.flat).isNull())
          continue;
        const auto &history = data.history;
        if (!history->loaded || !history->baseline)
          fail();
        stream << set.id << layer.id << quint8(role) << quint32(history->operations.size());
        write_raster(stream, *history->baseline);
        for (const auto &op : history->operations) {
          stream << op->spatial << op->erase << op->opacity;
          write_raster(stream, *op->ink);
        }
        if (stream.status() != QDataStream::Ok)
          fail();
      }
  quint32 effects = 0;
  for (const auto &set : document.texture_sets())
    visit_effect_paint(set, [&](QUuid, QUuid, bool, const EffectPaint &) { ++effects; });
  if (effects) {
    stream << quint32(0x42544531) << effects;
    std::uint64_t effect_bytes = writer.logical_size();
    for (const auto &set : document.texture_sets())
      visit_effect_paint(set, [&](QUuid parent, QUuid id, bool mask, const EffectPaint &paint) {
        stream << set.id << parent << id << mask;
        write_effect_paint(stream, paint, effect_bytes, budget);
        if (stream.status() != QDataStream::Ok)
          fail();
      });
  }
  quint32 path_effects = 0;
  for (const auto &set : document.texture_sets())
    visit_effect_paint(set, [&](QUuid, QUuid, bool, const EffectPaint &paint) {
      for (int role = 0; role < 10; ++role) {
        const auto history = effect_history(paint, role);
        const StrokeRaster flat = effect_flat(paint, role);
        path_effects += !stroke_history_json(history, flat).isNull();
      }
    });
  if (path_effects) {
    stream << quint32(0x42545031) << path_effects;
    for (const auto &set : document.texture_sets())
      visit_effect_paint(set, [&](QUuid parent, QUuid id, bool mask, const EffectPaint &paint) {
        for (int role = 0; role < 10; ++role) {
          const auto history = effect_history(paint, role);
          const StrokeRaster flat = effect_flat(paint, role);
          if (stroke_history_json(history, flat).isNull())
            continue;
          if (!history->loaded || !history->baseline)
            fail();
          stream << set.id << parent << id << mask << quint8(role)
                 << quint32(history->operations.size());
          write_raster(stream, *history->baseline);
          for (const auto &op : history->operations) {
            stream << op->spatial << op->erase << op->opacity;
            write_raster(stream, *op->ink);
          }
          if (stream.status() != QDataStream::Ok)
            fail();
        }
      });
  }
  if (stream.status() != QDataStream::Ok)
    fail();
  return writer.finish();
}
void read_stroke_archive(TextureDocument &document, const QByteArray &bytes)
{
  const auto budget = history_archive::decoded_budget();
  HistoryArchiveReader reader(bytes);
  QDataStream stream(&reader);
  stream.setVersion(QDataStream::Qt_6_8);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
  quint32 magic, entries;
  stream >> magic >> entries;
  if (magic != 0x42545331)
    fail();
  std::set<std::pair<QUuid, int>> seen;
  std::uint64_t decoded = 0;
  for (quint32 e = 0; e < entries; ++e) {
    QUuid set_id, layer_id;
    quint8 role;
    quint32 operations;
    stream >> set_id >> layer_id >> role >> operations;
    if (role >= 3 + custom_channel_count || !seen.insert({layer_id, int(role)}).second)
      fail();
    const auto found = std::find_if(document.texture_sets().begin(),
                                    document.texture_sets().end(),
                                    [&](const auto &set) { return set.id == set_id; });
    if (found == document.texture_sets().end())
      fail();
    auto &set = document.set(found->material_session_uid);
    auto &layer = TextureDocument::layer(set, layer_id);
    if (role == 1 && !layer.mask)
      fail();
    const auto data = history_data(set, layer, role);
    auto &slot = history_reference(layer, role);
    if (!slot || slot->loaded || operations != quint32(slot->declared_operations) ||
        !slot->matches(data.flat))
      fail();
    auto history = std::make_shared<StrokeHistory>(*slot);
    history->loaded = true;
    history->baseline = std::make_shared<StrokeRaster>(
        read_raster(stream, data.resolution, set.tiles, decoded, budget));
    int spatial_count = 0;
    for (quint32 i = 0; i < operations; ++i) {
      auto op = std::make_shared<StrokeOperation>();
      stream >> op->spatial >> op->erase >> op->opacity;
      op->ink = std::make_shared<StrokeRaster>(
          read_raster(stream, data.resolution, set.tiles, decoded, budget));
      op->bounds = stroke_bounds(*op->ink);
      const auto path = history->declared_paths.find(int(i));
      if (path != history->declared_paths.end())
        op->path = path->second;
      spatial_count += op->spatial;
      history->operations.push_back(op);
    }
    if (spatial_count != slot->declared_spatial ||
        stroke_hashes(replay_strokes(*history, data.resolution)) != history->final_hashes())
      fail();
    slot = history;
  }
  if (!stream.atEnd()) {
    quint32 marker, effects;
    stream >> marker >> effects;
    if (marker != 0x42544531 || effects > 65536)
      fail();
    std::set<std::tuple<QUuid, QUuid, QUuid, bool>> loaded;
    for (quint32 i = 0; i < effects; ++i) {
      QUuid set_id, parent, id;
      bool mask;
      stream >> set_id >> parent >> id >> mask;
      if (!loaded.insert({set_id, parent, id, mask}).second)
        fail();
      const auto found = std::find_if(document.texture_sets().begin(),
                                      document.texture_sets().end(),
                                      [&](const auto &set) { return set.id == set_id; });
      if (found == document.texture_sets().end())
        fail();
      auto &set = document.set(found->material_session_uid);
      EffectPaint *target = nullptr;
      visit_effect_paint(set, [&](QUuid p, QUuid e, bool m, EffectPaint &paint) {
        if (p == parent && e == id && m == mask)
          target = &paint;
      });
      if (!target || target->loaded)
        fail();
      read_effect_paint(stream, *target, set.tiles, decoded, budget, mask);
    }
  }
  if (!stream.atEnd()) {
    quint32 marker, entries;
    stream >> marker >> entries;
    if (marker != 0x42545031)
      fail();
    std::set<std::tuple<QUuid, QUuid, QUuid, bool, int>> loaded;
    for (quint32 i = 0; i < entries; ++i) {
      QUuid set_id, parent, id;
      bool mask;
      quint8 role;
      quint32 operations;
      stream >> set_id >> parent >> id >> mask >> role >> operations;
      if (role > 9 || !loaded.insert({set_id, parent, id, mask, int(role)}).second)
        fail();
      const auto found = std::find_if(document.texture_sets().begin(),
                                      document.texture_sets().end(),
                                      [&](const auto &set) { return set.id == set_id; });
      if (found == document.texture_sets().end())
        fail();
      auto &set = document.set(found->material_session_uid);
      EffectPaint *paint = nullptr;
      visit_effect_paint(set, [&](QUuid p, QUuid e, bool m, EffectPaint &value) {
        if (p == parent && e == id && m == mask)
          paint = &value;
      });
      if (!paint || !paint->loaded)
        fail();
      auto &slot = effect_history(*paint, role);
      const StrokeRaster flat = effect_flat(*paint, role);
      if (!slot || slot->loaded || operations != quint32(slot->declared_operations) ||
          !slot->matches(flat))
        fail();
      auto history = std::make_shared<StrokeHistory>(*slot);
      history->loaded = true;
      history->baseline = std::make_shared<StrokeRaster>(
          read_raster(stream, paint->resolution, set.tiles, decoded, budget));
      int spatial = 0;
      for (quint32 op_index = 0; op_index < operations; ++op_index) {
        auto op = std::make_shared<StrokeOperation>();
        stream >> op->spatial >> op->erase >> op->opacity;
        op->ink = std::make_shared<StrokeRaster>(
            read_raster(stream, paint->resolution, set.tiles, decoded, budget));
        op->bounds = stroke_bounds(*op->ink);
        const auto metadata = history->declared_paths.find(int(op_index));
        if (metadata != history->declared_paths.end())
          op->path = metadata->second;
        spatial += op->spatial;
        history->operations.push_back(op);
      }
      if (spatial != slot->declared_spatial ||
          stroke_hashes(replay_strokes(*history, paint->resolution)) != history->final_hashes())
        fail();
      slot = std::move(history);
    }
  }
  for (const auto &set : document.texture_sets())
    visit_effect_paint(set, [&](QUuid, QUuid, bool, const EffectPaint &paint) {
      if (!paint.loaded)
        fail();
      if (paint.strokes && !paint.strokes->loaded)
        fail();
      for (const auto &history : paint.user_strokes)
        if (history && !history->loaded)
          fail();
      if (paint.displacement_strokes && !paint.displacement_strokes->loaded)
        fail();
    });
  if (!stream.atEnd() || stream.status() != QDataStream::Ok || !reader.complete())
    fail();
  for (const auto &set : document.texture_sets())
    for (const auto &layer : set.layers)
      for (int role = 0; role < 3 + custom_channel_count; ++role) {
        const auto data = history_data(set, layer, role);
        if (data.history && !data.history->loaded)
          fail();
      }
  document.resolve_instances();
}
}  // namespace bts
