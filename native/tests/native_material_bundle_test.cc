// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialBundle.hh"
#include <cassert>
int main()
{
  using namespace bts;
  const QByteArray blend("opaque compressed Blender source");
  const QString hash(64, 'a');
  const std::vector<NativeMaterialImageReference> images{{"Color", hash, "color.png"},
                                                         {"Roughness", hash, "color.png"}};
  const auto bytes = native_material_bundle_bytes(blend, images);
  const auto read = read_native_material_bundle(bytes);
  assert(read.external && read.blend == blend && read.images.size() == 2 &&
         read.images[0].sha256 == read.images[1].sha256);
  assert(native_material_bundle_bytes(read.blend, read.images) == bytes);
  const QByteArray preview("encoded-png-preview");
  const auto with_preview = native_material_bundle_bytes(blend, images, {}, preview);
  assert(native_material_bundle_preview(with_preview) == preview);
  assert(read_native_material_bundle(with_preview).preview_png == preview);
  assert(native_material_source_hash(with_preview) == native_material_source_hash(bytes));
  auto changed_image = images;
  changed_image[0].sha256 = QString(64, 'b');
  assert(native_material_source_hash(native_material_bundle_bytes(blend, changed_image)) !=
         native_material_source_hash(bytes));
  assert(native_material_source_hash(native_material_bundle_bytes(
             blend + "different graph", images)) != native_material_source_hash(bytes));
  assert(native_material_bundle_preview(bytes).isEmpty());
  auto legacy = read_native_material_bundle(blend);
  assert(!legacy.external && legacy.blend == blend && legacy.images.empty());
  assert(read_native_material_bundle(native_material_bundle_bytes(blend, {})).images.empty());
  assert(native_material_source_hash(blend) ==
         native_material_source_hash(native_material_bundle_bytes(blend, {}, {}, preview)));
  const auto rejects = [](auto action) {
    try {
      action();
      return false;
    }
    catch (const std::invalid_argument &) {
      return true;
    }
  };
  assert(rejects([&] { read_native_material_bundle(bytes.first(12)); }));
  auto extra = bytes;
  extra.append('x');
  assert(rejects([&] { read_native_material_bundle(extra); }));
  auto bad = images;
  bad[0].filename = "../escape.png";
  assert(rejects([&] { native_material_bundle_bytes(blend, bad); }));
  bad = images;
  bad[1].image = bad[0].image;
  assert(rejects([&] { native_material_bundle_bytes(blend, bad); }));
  bad = images;
  bad[0].sha256 = QString(64, 'A');
  assert(rejects([&] { native_material_bundle_bytes(blend, bad); }));
  auto utf8_names = images;
  utf8_names[0].image = QString(127, QChar(0x00e9)) + "a";
  assert(utf8_names[0].image.toUtf8().size() == 255);
  assert(read_native_material_bundle(native_material_bundle_bytes(blend, utf8_names))
             .images[0]
             .image == utf8_names[0].image);
  utf8_names[0].image = QString(128, QChar(0x00e9));
  assert(rejects([&] { native_material_bundle_bytes(blend, utf8_names); }));
  auto long_names = images;
  long_names[0].filename = QString(5000, 'p') + ".exr";
  const auto long_bundle = native_material_bundle_bytes(blend, long_names);
  assert(read_native_material_bundle(long_bundle).images[0].filename == long_names[0].filename);
  {
    TextureRamQueryScope low([]() -> std::uint64_t { return 1024; });
    bool denied = false;
    try {
      long_names[0].validate();
    }
    catch (const std::length_error &) {
      denied = true;
    }
    assert(!denied);
    denied = false;
    try {
      read_native_material_bundle(long_bundle);
    }
    catch (const std::length_error &) {
      denied = true;
    }
    assert(!denied);
  }
  bad = long_names;
  bad[0].filename = QString::fromUtf8("bad\0name", 8);
  assert(rejects([&] { native_material_bundle_bytes(blend, bad); }));
}
