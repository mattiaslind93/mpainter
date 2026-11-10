// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpsmBitmap.hh"
#include "../../host/ResourceAvailability.hh"
#include <lz4.h>
#include <algorithm>
#include <bit>
#include <limits>
#include <set>

namespace bts::spsm::bitmap {
namespace {
class SizeAccounting {
  std::uint64_t used_ = 0, retained_ = 0;

 public:
  explicit SizeAccounting(ReadOptions) {}
  void add(std::uint64_t n)
  {
    used_ = resource_sum(used_, n);
  }
  void retain(std::uint64_t n)
  {
    retained_ = resource_sum(retained_, n);
  }
};
std::uint64_t header_memory(const Header &h)
{
  std::uint64_t bytes = resource_sum(sizeof(Header) + 128,
                                     resource_product(h.fields.capacity(), sizeof(HeaderField)));
  bytes = resource_sum(bytes, resource_sum(h.version.capacity(), h.compression.capacity()));
  if (h.encoded)
    bytes = resource_sum(bytes, h.encoded->capacity());
  for (const auto &f : h.fields) {
    bytes = resource_sum(bytes, resource_sum(f.key.utf16_be.capacity(), f.key.utf8.capacity()));
    if (const auto *t = std::get_if<Text>(&f.value))
      bytes = resource_sum(bytes, resource_sum(t->utf16_be.capacity(), t->utf8.capacity()));
    if (const auto *v = std::get_if<ImageBytes>(&f.value))
      bytes = resource_sum(bytes, v->capacity());
  }
  return bytes;
}
using Fetch = std::function<std::shared_ptr<const ImageBytes>(std::uint64_t, std::uint64_t)>;
class Parser {
  const Fetch &fetch_;
  std::uint64_t total_, at_ = 0;
  SizeAccounting budget_;
  Header result_;
  ImageBytes header_;
  void need(std::uint64_t n) const
  {
    if (at_ > total_ || n > total_ - at_)
      throw Error("Image header crosses record boundary");
  }
  std::shared_ptr<const ImageBytes> take(std::uint64_t n)
  {
    need(n);
    if (n > ImageBytes{}.max_size() - header_.size())
      throw Error("Image header exceeds native byte storage");
    budget_.add(resource_product(n, 3));  // Read buffer plus growing retained header.
    const auto bytes = fetch_(at_, n);
    if (!bytes || bytes->size() != n)
      throw Error("Truncated image header");
    header_.insert(header_.end(), bytes->begin(), bytes->end());
    at_ += n;
    return bytes;
  }
  template<class T> T number()
  {
    static_assert(std::is_unsigned_v<T>);
    const auto bytes = take(sizeof(T));
    T value = 0;
    for (const auto byte : *bytes)
      value = T((value << 8) | byte);
    return value;
  }
  static void utf8(std::string &out, std::uint32_t c)
  {
    if (c < 0x80)
      out.push_back(char(c));
    else if (c < 0x800) {
      out.push_back(char(0xc0 | (c >> 6)));
      out.push_back(char(0x80 | (c & 63)));
    }
    else if (c < 0x10000) {
      out.push_back(char(0xe0 | (c >> 12)));
      out.push_back(char(0x80 | ((c >> 6) & 63)));
      out.push_back(char(0x80 | (c & 63)));
    }
    else {
      out.push_back(char(0xf0 | (c >> 18)));
      out.push_back(char(0x80 | ((c >> 12) & 63)));
      out.push_back(char(0x80 | ((c >> 6) & 63)));
      out.push_back(char(0x80 | (c & 63)));
    }
  }
  Text text()
  {
    const auto n = number<std::uint32_t>();
    Text result;
    if (n == UINT32_MAX) {
      result.null = true;
      return result;
    }
    if (n & 1)
      throw Error("Odd UTF-16 image header string length");
    need(n);
    budget_.add(resource_product(n, 3));
    const auto bytes = take(n);
    result.utf16_be = *bytes;
    result.utf8.reserve(resource_product(n, 2));
    const auto code = [&](std::size_t i) {
      return (std::uint32_t((*bytes)[i]) << 8) | (*bytes)[i + 1];
    };
    for (std::size_t i = 0; i < bytes->size(); i += 2) {
      auto c = code(i);
      if (c >= 0xd800 && c <= 0xdbff) {
        if (i + 3 >= bytes->size())
          throw Error("Truncated UTF-16 image header surrogate");
        const auto low = code(i + 2);
        if (low < 0xdc00 || low > 0xdfff)
          throw Error("Invalid UTF-16 image header surrogate");
        c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
        i += 2;
      }
      else if (c >= 0xdc00 && c <= 0xdfff)
        throw Error("Unpaired UTF-16 image header surrogate");
      utf8(result.utf8, c);
    }
    return result;
  }
  HeaderValue value(std::uint32_t type)
  {
    if (type == 0)
      return std::monostate{};
    if (type == 1) {
      const auto b = number<std::uint8_t>();
      if (b > 1)
        throw Error("Invalid image header Boolean");
      return b != 0;
    }
    if (type == 2)
      return std::int64_t(std::bit_cast<std::int32_t>(number<std::uint32_t>()));
    if (type == 3)
      return std::uint64_t(number<std::uint32_t>());
    if (type == 4)
      return std::bit_cast<std::int64_t>(number<std::uint64_t>());
    if (type == 5)
      return number<std::uint64_t>();
    if (type == 6)
      return DoubleBits{number<std::uint64_t>()};
    if (type == 10)
      return text();
    if (type == 12) {
      const auto n = number<std::uint32_t>();
      if (n == UINT32_MAX)
        return std::monostate{};
      need(n);
      budget_.add(n);
      return ImageBytes(*take(n));
    }
    throw Error("Unsupported image header QVariant type " + std::to_string(type));
  }
  std::uint64_t integer(std::string_view name, bool optional = false)
  {
    const auto *f = result_.field(name);
    if (!f) {
      if (optional)
        return 0;
      throw Error("Image header missing " + std::string(name));
    }
    if (f->source_null)
      throw Error("Null required image header integer");
    if (const auto *n = std::get_if<std::uint64_t>(&f->value))
      return *n;
    if (const auto *n = std::get_if<std::int64_t>(&f->value); n && *n >= 0)
      return std::uint64_t(*n);
    throw Error("Invalid image header integer " + std::string(name));
  }
  std::string string(std::string_view name)
  {
    const auto *f = result_.field(name);
    if (f && !f->source_null)
      if (const auto *s = std::get_if<Text>(&f->value); s && !s->null)
        return s->utf8;
    throw Error("Invalid image header string " + std::string(name));
  }

 public:
  Parser(const Fetch &fetch, std::uint64_t total, ReadOptions o)
      : fetch_(fetch), total_(total), budget_(o)
  {
  }
  std::shared_ptr<const Header> run()
  {
    const auto count = number<std::uint32_t>();
    if (count > (total_ - at_) / 11)
      throw Error("Image header field count exceeds framing bytes");
    budget_.add(resource_product(count, sizeof(HeaderField)));
    budget_.add(resource_product(count, sizeof(std::string_view)));
    result_.fields.reserve(count);
    std::set<std::string> names;
    for (std::uint32_t i = 0; i < count; ++i) {
      HeaderField f;
      f.key = text();
      if (f.key.null || f.key.utf8.empty() || f.key.utf8.find('\0') != std::string::npos)
        throw Error("Invalid image header field name");
      budget_.add(resource_sum(resource_product(f.key.utf8.size(), 2),
                               sizeof(std::string) + 4 * sizeof(void *)));
      if (!names.insert(f.key.utf8).second)
        throw Error("Duplicate image header field");
      f.source_type = number<std::uint32_t>();
      f.source_null = number<std::uint8_t>();
      if (f.source_null > 1)
        throw Error("Invalid QVariant null flag");
      f.value = value(f.source_type);
      result_.fields.push_back(std::move(f));
    }
    result_.version = string("version");
    result_.compression = string("compression");
    if (result_.version != "v1" && result_.version != "v2")
      throw Error("Unsupported image record version");
    if (result_.compression != "lz4")
      throw Error("Unsupported image compression");
    result_.width = integer("width");
    result_.height = integer("height");
    result_.pitch = integer("pitch");
    result_.raw_bytes = integer("dataSizeRaw");
    result_.compressed_bytes = integer("dataSizeLZ4");
    result_.icc_bytes = integer("ICCdataSize", true);
    for (const auto key : {"format", "flags", "colorSpace", "alphaFormat"})
      if (!result_.field(key))
        throw Error("Image header missing raw interpretation metadata");
    if (!result_.width || !result_.height || !result_.pitch || !result_.raw_bytes ||
        !result_.compressed_bytes)
      throw Error("Empty image dimensions/byte spans");
    if (resource_product(result_.pitch, result_.height) != result_.raw_bytes)
      throw Error("Image row span differs from declared raw byte count");
    result_.payload_offset = at_;
    result_.record_bytes = total_;
    if (resource_sum(at_, resource_sum(result_.compressed_bytes, result_.icc_bytes)) != total_)
      throw Error("Image compressed/ICC spans differ from dataset extent");
    budget_.add(sizeof(Header) + sizeof(ImageBytes) + 128);
    result_.encoded = std::make_shared<const ImageBytes>(std::move(header_));
    return std::make_shared<const Header>(std::move(result_));
  }
};
}  // namespace
const HeaderField *Header::field(std::string_view name) const
{
  const auto found = std::find_if(
      fields.begin(), fields.end(), [&](const HeaderField &f) { return f.key.utf8 == name; });
  return found == fields.end() ? nullptr : &*found;
}
std::shared_ptr<const Header> parse_header(std::span<const std::uint8_t> prefix,
                                           std::uint64_t total,
                                           ReadOptions options)
{
  const Fetch fetch = [prefix](std::uint64_t offset, std::uint64_t count) {
    if (offset > prefix.size() || count > prefix.size() - offset)
      throw Error("Truncated image header prefix");
    return std::make_shared<const ImageBytes>(prefix.begin() + std::size_t(offset),
                                              prefix.begin() + std::size_t(offset + count));
  };
  return Parser(fetch, total, options).run();
}
Descriptor describe_image(const Archive &archive, std::string_view dataset, ReadOptions options)
{
  auto resource = describe_image_resource(archive, dataset, options);
  std::uint64_t read = 0;
  const Fetch fetch = [&](std::uint64_t offset, std::uint64_t count) {
    auto bytes = read_image_range(resource, offset, count, options);
    read = resource_sum(read, count);
    return bytes;
  };
  auto header = Parser(fetch, resource.dataset.size, options).run();
  return {std::move(resource), std::move(header), read};
}
std::shared_ptr<const EncodedImage> load_encoded(const Descriptor &descriptor, ReadOptions options)
{
  if (!descriptor.header || descriptor.header->record_bytes != descriptor.resource.dataset.size)
    throw Error("Missing/mismatched image descriptor");
  SizeAccounting sizes(options);
  sizes.retain(header_memory(*descriptor.header));
  sizes.add(resource_sum(descriptor.resource.dataset.size,
                         sizeof(EncodedImage) + sizeof(ImageBytes) + 128));
  auto bytes = read_image_range(descriptor.resource, 0, descriptor.resource.dataset.size, options);
  if (descriptor.header->encoded->size() > bytes->size() ||
      !std::equal(
          descriptor.header->encoded->begin(), descriptor.header->encoded->end(), bytes->begin()))
    throw Error("Image header changed before hydration");
  return std::make_shared<const EncodedImage>(EncodedImage{descriptor.header, std::move(bytes)});
}
RawImage decode(std::shared_ptr<const EncodedImage> encoded, ReadOptions options)
{
  if (!encoded || !encoded->header || !encoded->record)
    throw Error("Missing encoded image ownership");
  const auto &h = *encoded->header;
  if (h.icc_bytes)
    throw Error(
        "Nonempty image ICC payload placement is unverified; raw record retained without decoding");
  if (h.raw_bytes > std::uint64_t(std::numeric_limits<int>::max()) ||
      h.compressed_bytes > std::uint64_t(std::numeric_limits<int>::max()))
    throw Error("Image LZ4 block exceeds the real signed-int codec API representation");
  if (!h.raw_bytes || !h.compressed_bytes || h.payload_offset > encoded->record->size() ||
      h.compressed_bytes != encoded->record->size() - h.payload_offset ||
      h.raw_bytes > ImageBytes{}.max_size())
    throw Error("Invalid encoded image byte spans");
  SizeAccounting sizes(options);
  sizes.retain(resource_sum(encoded->record->capacity(), header_memory(h)));
  sizes.add(resource_sum(h.raw_bytes, sizeof(ImageBytes) + 128));
  auto bytes = std::make_shared<ImageBytes>(std::size_t(h.raw_bytes));
  const auto produced = LZ4_decompress_safe(
      reinterpret_cast<const char *>(encoded->record->data() + h.payload_offset),
      reinterpret_cast<char *>(bytes->data()),
      int(h.compressed_bytes),
      int(h.raw_bytes));
  if (produced < 0 || std::uint64_t(produced) != h.raw_bytes)
    throw Error("Corrupt/truncated LZ4 image block or raw size mismatch");
  return {std::move(encoded), std::move(bytes)};
}
}  // namespace bts::spsm::bitmap
