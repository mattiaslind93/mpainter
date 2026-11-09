// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SpsmReader.hh"
#include <functional>
#include <variant>

namespace bts::spsm::bitmap {
// Exact Qt-style header representation, without adopting its pixel/color enums.
struct Text {std::vector<std::uint8_t> utf16_be; std::string utf8; bool null=false;};
struct DoubleBits {std::uint64_t bits=0;};
using HeaderValue=std::variant<std::monostate,bool,std::int64_t,std::uint64_t,
                               DoubleBits,Text,ImageBytes>;
struct HeaderField {
 Text key;
 std::uint32_t source_type=0;
 std::uint8_t source_null=0;
 HeaderValue value;
};
struct Header {
 std::vector<HeaderField> fields; // Preserve order, unknown fields and raw enums.
 std::shared_ptr<const ImageBytes> encoded;
 std::string version,compression;
 std::uint64_t width=0,height=0,pitch=0,raw_bytes=0,compressed_bytes=0,icc_bytes=0;
 std::uint64_t payload_offset=0,record_bytes=0;
 const HeaderField *field(std::string_view) const;
};
// Prefix parsing never touches bytes after the header, even if a full record is
// supplied. record_bytes is the independently validated full resource extent.
std::shared_ptr<const Header> parse_header(std::span<const std::uint8_t> prefix,
                                          std::uint64_t record_bytes,ReadOptions={});
struct Descriptor {
 ImageResource resource;
 std::shared_ptr<const Header> header;
 std::uint64_t header_bytes_read=0;
};
Descriptor describe_image(const Archive &,std::string_view dataset,ReadOptions={});
struct EncodedImage {
 std::shared_ptr<const Header> header;
 std::shared_ptr<const ImageBytes> record; // Original header + compressed bytes.
};
std::shared_ptr<const EncodedImage> load_encoded(const Descriptor &,ReadOptions={});
enum class Interpretation {UnverifiedStoredBytes};
struct RawImage {
 std::shared_ptr<const EncodedImage> encoded;
 std::shared_ptr<const ImageBytes> bytes; // Byte-exact LZ4 output, with row padding.
 Interpretation row_order=Interpretation::UnverifiedStoredBytes;
 Interpretation component_order=Interpretation::UnverifiedStoredBytes;
 Interpretation color=Interpretation::UnverifiedStoredBytes;
 Interpretation alpha=Interpretation::UnverifiedStoredBytes;
 // No conversion to RGBA, sRGB, linear color, alpha or native GPU textures.
};
RawImage decode(std::shared_ptr<const EncodedImage>,ReadOptions={});
} // namespace bts::spsm::bitmap
