// SPDX-License-Identifier: GPL-3.0-or-later
#include "ImageSequence.hh"
#include "SmartPresetName.hh"
#include "FillTextureJSON.hh"
#include <QTemporaryDir>
#include <cassert>
#include <cstdlib>
#include <iostream>
using namespace bts;
template<class F> void rejected(F action)
{
  bool caught = false;
  try {
    action();
  }
  catch (const std::exception &) {
    caught = true;
  }
  assert(caught);
}
std::shared_ptr<FillTexture> image(FillTexture::Pixel value,
                                   int width,
                                   int height,
                                   bool raw = false)
{
  auto result = std::make_shared<FillTexture>();
  result->asset = "c0a80001-1234-4000-8000-000000000001";
  result->hash = std::string(64, 'a');
  result->name = "Sequence";
  result->width = width;
  result->height = height;
  result->raw = raw;
  result->pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(width * height, value);
  return result;
}
int main()
{
  for (const QString name : {"file_22.1032.jpg",
                             "file_22-223.1032.jpg",
                             "file_22-223-1032.jpg",
                             "file_22-223_1032.jpg"})
    assert(sequence_filename(name)->tile == 1032);
  for (const QString name : {"file_22-2232032.jpg",
                             "file_22-223PM2032.jpg",
                             "file_22-223-0032.jpg",
                             "file_22-223_Rec2020.jpg",
                             "file_1000.png",
                             "file_1001_extra.png"})
    assert(!sequence_filename(name));
  const QString root = qEnvironmentVariable("BTS_ROOT", QDir::currentPath());
  QDir().mkpath(root + "/build");
  QTemporaryDir dir(root + "/build/image-sequence-XXXXXX");
  assert(dir.isValid());
  const auto write = [&](const QString &name, const QByteArray &bytes) {
    QFile f(dir.filePath(name));
    assert(f.open(QIODevice::WriteOnly));
    assert(f.write(bytes) == bytes.size());
  };
  write("pair_1001.png", "first image");
  write("pair.1002.png", "second image");
  write("pair_1002.exr", "different extension");
  write("other_1003.png", "other sequence");
  write("plain.png", "one image");
  const auto grouped = read_image_resource(dir.filePath("pair_1001.png"));
  assert(grouped.name == "pair" && grouped.filename == "pair.btsseq" && grouped.tiles == 2 &&
         is_image_sequence(grouped.bytes));
  const auto members = read_image_sequence(grouped.bytes);
  assert(members.size() == 2 && members[0].tile == 1001 && members[1].tile == 1002 &&
         members[1].bytes == "second image");
  assert(make_image_sequence(members) == grouped.bytes);
  assert(read_image_resource(dir.filePath("pair.1002.png")).bytes == grouped.bytes);
  assert(read_image_resource(dir.filePath("plain.png")).tiles == 0);
  assert(!is_image_sequence(read_image_resource(dir.filePath("pair_1001.png"), false).bytes));
  rejected([&] { read_image_sequence(grouped.bytes + "tail"); });
  rejected([&] { read_image_sequence(grouped.bytes.first(10)); });
  auto duplicate = members;
  duplicate[1].tile = 1001;
  rejected([&] { make_image_sequence(duplicate); });
  auto wrong = members;
  wrong[1].filename = "bad_1003.png";
  rejected([&] { make_image_sequence(wrong); });
  write("pair-1001.png", "duplicate");
  rejected([&] { read_image_resource(dir.filePath("pair_1001.png")); });
  auto red = image({1, 0, 0, 1}, 2, 1), green = image({0, 1, 0, .5}, 1, 2);
  auto sequence = std::make_shared<FillTexture>(*red);
  sequence->tiles = std::make_shared<const std::map<int, std::shared_ptr<const FillTexture>>>(
      std::map<int, std::shared_ptr<const FillTexture>>{{1001, red}, {1002, green}});
  sequence->validate();
  FillProjection mapping;
  mapping.mode = 6;
  mapping.texture = sequence;
  mapping.scale = {7, 7};
  mapping.offset = {10, 20};
  mapping.physical = true;
  mapping.rotation = 95;
  assert(
      (fill_tile_match_sample(mapping, 4, 40, 128, 256, 1001) == FillTexture::Pixel{1, 0, 0, 1}));
  assert((fill_tile_match_sample(mapping, 100, 10, 128, 256, 1002) ==
          FillTexture::Pixel{0, 1, 0, .5}));
  assert((fill_tile_match_sample(mapping, 64, 64, 128, 128, 1003) == FillTexture::Pixel{}));
  mapping.texture = red;
  assert(
      (fill_tile_match_sample(mapping, 64, 64, 128, 128, 1002) == FillTexture::Pixel{1, 0, 0, 1}));
  const auto descriptor = fill_texture_json(sequence);
  const auto unresolved = read_fill_texture_json(descriptor);
  assert(!unresolved->pixels && unresolved->tiles->size() == 2 &&
         !unresolved->tiles->at(1002)->pixels);
  assert(fill_texture_json(unresolved) == descriptor);
  auto broken = descriptor.toObject();
  auto children = broken["tiles"].toArray();
  children.append(children[0]);
  broken["tiles"] = children;
  rejected([&] { read_fill_texture_json(broken); });
  auto signed_image = image({-.5, 2, 3, 1}, 2, 2, true);
  auto signed_sequence = std::make_shared<FillTexture>(*signed_image);
  signed_sequence->tiles =
      std::make_shared<const std::map<int, std::shared_ptr<const FillTexture>>>(
          std::map<int, std::shared_ptr<const FillTexture>>{{1002, signed_image}});
  mapping.texture = signed_sequence;
  assert(fill_tile_match_sample(mapping, 64, 64, 128, 128, 1002)[0] == -.5f);
  auto large = image({0, 0, 0, 1}, 1, 1);
  large->pixels.reset();
  large->width = large->height = 4000;
  auto over_budget = std::make_shared<FillTexture>(*large);
  over_budget->tiles = std::make_shared<const std::map<int, std::shared_ptr<const FillTexture>>>(
      std::map<int, std::shared_ptr<const FillTexture>>{{1001, large}, {1002, large}});
  over_budget->validate();
  large->width = 8193;
  rejected([&] { over_budget->validate(); });
  auto edge = image({0, 0, 0, 1}, 2, 1);
  auto edge_pixels = std::make_shared<std::vector<FillTexture::Pixel>>(
      std::initializer_list<FillTexture::Pixel>{{1, 0, 0, 1}, {0, 0, 1, 1}});
  edge->pixels = edge_pixels;
  assert(edge->sample(0, .5, false)[0] == 1 && edge->sample(1, .5, false)[2] == 1);

  const QString long_name = QString(70000, 's');
  assert(smart_preset_name("  " + long_name + "  ") == long_name);
  rejected([&] { smart_preset_name("   "); });
  rejected([&] { smart_preset_name(QString::fromUtf8("bad\0name", 8)); });
  const std::vector<SequenceMember> long_members{
      {1001, QString(5000, 'p') + "_1001.exr", "one image"}};
  const auto long_bundle = make_image_sequence(long_members);
  assert(read_image_sequence(long_bundle)[0].filename == long_members[0].filename);
  {
    TextureRamQueryScope low([]() -> std::uint64_t { return 1024; });
    ([&] { smart_preset_name(long_name); })();
    ([&] { read_image_sequence(long_bundle); })();
    ([&] { make_image_sequence(long_members); })();
  }
  const auto huge_path = dir.filePath("large.hdr");
  {
    QFile sparse(huge_path);
    assert(sparse.open(QIODevice::WriteOnly));
    assert(sparse.resize(256LL * 1024 * 1024 + 1));
  }
  {
    TextureRamQueryScope low([]() -> std::uint64_t { return 1024; });
    ([&] { read_image_resource(huge_path, false); })();
  }
  {
    TextureRamQueryScope enough([]() -> std::uint64_t { return 2ULL * 1024 * 1024 * 1024; });
    const auto admitted = read_image_resource(huge_path, false);
    assert(admitted.bytes.size() == 256LL * 1024 * 1024 + 1);
  }
  std::cout
      << "UDIM terminal names/mixed delimiters/discovery/grouping; strict bundle identity/order/bounds; per-tile rectangular, missing, raw signed and clamped samples; descriptors, preview identity and multi-tile capacity; long metadata, low RAM, >256 MiB encoded read passed\n";
}
