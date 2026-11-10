// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QCryptographicHash>
#include "../host/spsm-import/SpsmBitmapTexture.hh"
#include "../host/io/FillTextureJSON.hh"
#include "BLI_math_half.hh"
#include "IMB_colormanagement.hh"
#include "IMB_imbuf.hh"
#include "CLG_log.h"
#include <lz4.h>
#include <algorithm>
#include <bit>
#include <future>
#include <iostream>
using namespace bts;
using namespace bts::spsm;
using namespace bts::spsm::bitmap;
namespace {
void require(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
template<class F> void reject(F f, std::string_view why)
{
  try {
    f();
  }
  catch (const std::exception &e) {
    if (std::string_view(e.what()).find(why) != std::string_view::npos)
      return;
    throw std::runtime_error(std::string("Unexpected rejection: ") + e.what());
  }
  throw std::runtime_error("Ambiguous/unsafe image accepted");
}
struct Writer {
  ImageBytes data;
  template<class T> void number(T n)
  {
    for (std::size_t i = sizeof(T); i-- > 0;)
      data.push_back(std::uint8_t(std::uint64_t(n) >> (8 * i)));
  }
  void text(std::string_view s)
  {
    number(std::uint32_t(s.size() * 2));
    for (auto c : s) {
      data.push_back(0);
      data.push_back(std::uint8_t(c));
    }
  }
  void field(std::string_view key, unsigned type)
  {
    text(key);
    number(std::uint32_t(type));
    number(std::uint8_t(0));
  }
  void integer(std::string_view key, std::uint64_t n)
  {
    field(key, 5);
    number(n);
  }
  void string(std::string_view key, std::string_view s)
  {
    field(key, 10);
    text(s);
  }
};
RawImage synthetic(bool big = false, bool v1 = false, bool invalid = false, bool alpha_hdr = false)
{
  // Asymmetric rows, padding, negative/HDR/subnormal/signed-zero components.
  const std::uint16_t values[16] = {0xbc00,
                                    0x4000,
                                    0x0001,
                                    0x3c00,
                                    0x4200,
                                    0x8000,
                                    0x3800,
                                    0x3800,
                                    0x4400,
                                    0x4500,
                                    0x4600,
                                    0x3c00,
                                    0x4700,
                                    0x4800,
                                    0x4900,
                                    0x3c00};
  ImageBytes bytes(40, 0x5a);
  for (unsigned i = 0; i < 16; ++i) {
    const auto at = (i / 8) * 20 + (i % 8) * 2;
    const auto n = invalid && i == 0   ? std::uint16_t(0x7c00) :
                   alpha_hdr && i == 3 ? std::uint16_t(0x4000) :
                                         values[i];
    bytes[at + (big ? 1 : 0)] = std::uint8_t(n);
    bytes[at + (big ? 0 : 1)] = std::uint8_t(n >> 8);
  }
  ImageBytes compressed(std::size_t(LZ4_compressBound(int(bytes.size()))));
  const auto count = LZ4_compress_default(reinterpret_cast<const char *>(bytes.data()),
                                          reinterpret_cast<char *>(compressed.data()),
                                          int(bytes.size()),
                                          int(compressed.size()));
  require(count > 0, "fixture compression failed");
  compressed.resize(count);
  Writer w;
  w.number(std::uint32_t(11));
  w.integer("width", 2);
  w.integer("height", 2);
  w.integer("pitch", 20);
  w.string("version", v1 ? "v1" : "v2");
  if (v1)
    w.integer("format", 3);
  else
    w.string("format", "RGBA16F");
  w.integer("flags", 0);
  w.integer("dataSizeRaw", bytes.size());
  w.integer("dataSizeLZ4", compressed.size());
  w.string("compression", "lz4");
  w.integer("colorSpace", 987654);
  w.integer("alphaFormat", 12345);
  w.data.insert(w.data.end(), compressed.begin(), compressed.end());
  auto encoded = std::make_shared<const EncodedImage>(EncodedImage{
      parse_header(w.data, w.data.size()), std::make_shared<const ImageBytes>(std::move(w.data))});
  return decode(std::move(encoded));
}
ImageResource source()
{
  return {"/independent-fixture.spsm", {"/resources/known.image", 0}, {0, {}}};
}
void boundaries()
{
  SpsmBitmapPolicy p{
      SpsmByteOrder::LittleEndian, SpsmRowOrder::FirstRowIsV0, SpsmAlpha::Straight, true, {}};
  const auto raw = synthetic();
  reject([&] { interpret_spsm_bitmap(raw, source(), {}); }, "explicit byte order");
  const auto a = interpret_spsm_bitmap(raw, source(), p);
  require(a->texture.raw && a->texture.floating_point && a->texture.range_valid &&
              a->texture.asset.size() == 36 && a->texture.hash.size() == 64,
          "native resource contract missing");
  const auto &pixels = *a->texture.pixels;
  require(pixels[0][0] == -1 && pixels[0][1] == 2 && pixels[0][2] == std::ldexp(1.f, -24) &&
              pixels[0][3] == 1 && pixels[2][0] == 4,
          "known half/HDR/row/padding pixels wrong");
  require(std::signbit(pixels[1][1]), "signed zero lost");
  require(a->original.bytes == raw.bytes && !a->warnings.empty(),
          "immutable source or policy warnings lost");
  auto flipped = p;
  flipped.rows = SpsmRowOrder::FirstRowIsV1;
  const auto f = interpret_spsm_bitmap(raw, source(), flipped);
  require((*f->texture.pixels)[0][0] == 4 &&
              f->texture.storage_identity() != a->texture.storage_identity() &&
              f->texture.hash != a->texture.hash && f->encoded_sha256 == a->encoded_sha256,
          "row policy or storage identity conflated");
  auto be = p;
  be.bytes = SpsmByteOrder::BigEndian;
  const auto big = interpret_spsm_bitmap(synthetic(true), source(), be);
  require(*big->texture.pixels == pixels, "explicit big endian interpretation changed values");
  reject([&] { interpret_spsm_bitmap(synthetic(false, true), source(), p); }, "v1 integer format");
  reject([&] { interpret_spsm_bitmap(synthetic(false, false, true), source(), p); }, "nonfinite");
  const auto hdr_alpha = synthetic(false, false, false, true);
  reject([&] { interpret_spsm_bitmap(hdr_alpha, source(), p); }, "straight alpha");
  auto data_policy = p;
  data_policy.alpha = SpsmAlpha::DataComponents;
  require((*interpret_spsm_bitmap(hdr_alpha, source(), data_policy)->texture.pixels)[0][3] == 2,
          "raw fourth data component clipped");
  auto color = p;
  color.raw = false;
  reject([&] { interpret_spsm_bitmap(raw, source(), color); }, "explicit known input");
  color.input_space = "not-a-real-OCIO-space";
  reject([&] { interpret_spsm_bitmap(raw, source(), color); }, "Unknown SPSM input");
  color.input_space = "Non-Color";
  reject([&] { interpret_spsm_bitmap(raw, source(), color); }, "data color space");
  color.input_space = "ACEScg";
  const auto identity = interpret_spsm_bitmap(raw, source(), color);
  require(*identity->texture.pixels == pixels && !identity->texture.raw &&
              identity->texture.input_space == "ACEScg",
          "ACEScg identity conversion changed HDR/data");
  color.input_space = "sRGB";
  const auto converted = interpret_spsm_bitmap(raw, source(), color);
  float reference[4] = {-1, 2, std::ldexp(1.f, -24), 1};
  blender::IMB_colormanagement_transform_float(reference, 1, 1, 4, "sRGB", "ACEScg", false);
  for (int c = 0; c < 4; ++c)
    require((*converted->texture.pixels)[0][c] == reference[c],
            "color did not use configured OCIO conversion");
  require(converted->texture.asset == a->texture.asset &&
              converted->texture.hash != a->texture.hash &&
              converted->encoded_sha256 == a->encoded_sha256 &&
              converted->texture.storage_identity() != a->texture.storage_identity(),
          "external source binding/revision identity changed by interpretation");
  {
    TextureRamQueryScope low(+[]() -> std::uint64_t { return 32; });
    ([&] { interpret_spsm_bitmap(raw, source(), p); })();
  }
  interpret_spsm_bitmap(raw, source(), p, {});
  decode(raw.encoded, {});
  auto broken = std::make_shared<EncodedImage>(*raw.encoded);
  auto data = std::make_shared<ImageBytes>(*broken->record);
  std::fill(data->begin() + broken->header->payload_offset, data->end(), 0xff);
  broken->record = data;
  reject([&] { decode(broken); }, "LZ4");
  const auto persisted_a = read_fill_texture_json(fill_texture_json(a->binding()));
  const auto persisted_f = read_fill_texture_json(fill_texture_json(f->binding()));
  require(persisted_a->hash != persisted_f->hash && persisted_a->asset == persisted_f->asset,
          "serialized descriptors conflated byte/row interpretation");
  const auto retained = a->binding();
  require(retained->pixels == a->texture.pixels, "native binding copied pixels");
  std::cout
      << "PASS synthetic asymmetric half/byte/row/padded/HDR/negative/subnormal/alpha/OCIO/identities/lowRAM/ambiguous/corrupt contracts; no GPU\n";
}
void actual(const std::filesystem::path &directory)
{
  unsigned accepted = 0, unsupported = 0;
  SpsmBitmapTextureCache cache;
  for (const auto name : {"dirty-plastic-robot.spsm",
                          "shd-hard-rust.spsm",
                          "sratched-metal-pealing-paint-dirt.spsm",
                          "used_varnish_wood.spsm",
                          "worn leather chest.spsm"})
  {
    const auto path = directory / name;
    const auto before = std::filesystem::last_write_time(path);
    const auto before_size = std::filesystem::file_size(path);
    const auto archive = read_archive(path);
    for (const auto &d : archive.datasets) {
      if (!d.path.ends_with(".image") || !d.path.starts_with("/resources/") ||
          d.path.find('/', 11) != std::string::npos)
        continue;
      const auto descriptor = describe_image(archive, d.path);
      SpsmBitmapPolicy p{SpsmByteOrder::LittleEndian,
                         SpsmRowOrder::FirstRowIsV0,
                         SpsmAlpha::DataComponents,
                         true,
                         {}};
      if (descriptor.header->version == "v1") {
        reject([&] { cache.load(descriptor, p); }, "v1 integer format");
        ++unsupported;
        continue;
      }
      auto image = cache.load(descriptor, p);
      const auto binding = image->binding();
      const auto pixel_pointer = binding->pixels.get();
      const auto first = image->original.bytes;
      image.reset();
      const auto repeated = cache.load(descriptor, p);
      require(repeated->texture.pixels.get() == pixel_pointer && repeated->original.bytes == first,
              "live native binding did not share cache pixels/source ownership");
      const auto &bytes = *repeated->original.bytes;
      const auto &header = *repeated->original.encoded->header;
      const auto &pixels = *binding->pixels;
      for (const auto index :
           {std::size_t(0), pixels.size() / 3, pixels.size() / 2, pixels.size() - 1})
        for (unsigned c = 0; c < 4; ++c) {
          const auto y = index / header.width, x = index % header.width,
                     at = y * header.pitch + x * 8 + c * 2;
          const auto bits = std::uint16_t(bytes[at] | (std::uint16_t(bytes[at + 1]) << 8));
          require(pixels[index][c] == blender::math::half_to_float(bits),
                  "actual raw component differs from explicit half policy");
        }
      auto concurrent = std::async(std::launch::async, [&] { return cache.load(descriptor, p); });
      require(concurrent.get()->texture.pixels.get() == pixel_pointer,
              "concurrent repeated binding copied pixels");
      std::cout
          << "NATIVE_BITMAP " << name << ' ' << d.path
          << " encoded_sha256=" << repeated->encoded_sha256
          << " interpreted_sha256=" << binding->hash << " min=" << binding->minimum
          << " max=" << binding->maximum
          << " policy=explicit-little-endian/first-row-v0/raw-data-components appearance_verified=0\n";
      ++accepted;
    }
    require(std::filesystem::last_write_time(path) == before &&
                std::filesystem::file_size(path) == before_size,
            "source archive changed");
  }
  require(accepted == 6 && unsupported == 2, "actual eight image capability count changed");
  std::cout
      << "PASS all8 records: six v2 explicit-policy CPU bridges, two v1 refused, immutable cache/pixel ownership, sources unchanged; no source appearance claim\n";
}
}  // namespace
int main(int argc, char **argv)
{
  try {
    QCoreApplication app(argc, argv);
    std::cout << std::unitbuf;
    CLG_init();
    blender::IMB_init();
    if (argc > 1 && std::string_view(argv[1]) == "--runtime-baseline")
      std::cout << "PASS native runtime baseline only; no bitmap calls\n";
    else {
      boundaries();
      if (argc > 1)
        actual(argv[1]);
    }
    blender::IMB_exit();
    CLG_exit();
    return 0;
  }
  catch (const std::exception &e) {
    std::cerr << "SPSM native bitmap test failed: " << e.what() << '\n';
    return 1;
  }
}
