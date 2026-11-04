#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace bts::spsm {

// Structural data, not an executable graph or a native material. Wire tags and
// class/field names remain intact, including fields not understood by the app.
using NodeId = std::size_t;
struct FloatValues { std::array<float, 16> values{}; std::uint8_t count = 0; };
struct IntValues { std::array<std::int32_t, 4> values{}; std::uint8_t count = 0; };
struct Blob { std::uint64_t offset = 0, size = 0; };
struct Array { std::vector<NodeId> elements; };
struct Field { std::string name; NodeId value = 0; };
struct Object { std::string class_name; std::vector<Field> fields; };
using Value = std::variant<std::monostate, FloatValues, IntValues, std::int64_t,
                           std::uint64_t, bool, std::string, Blob, Array, Object>;
struct Node {
  std::uint8_t wire_tag = 0;
  std::uint64_t offset = 0, end = 0;
  Value value;
};
struct Preset {
  std::string dataset;
  std::uint32_t header_flags = 0, wire_version = 0;
  // Flat ownership prevents recursive parsing, traversal and destruction from
  // overflowing the process stack on deeply nested input.
  std::vector<Node> nodes;
  NodeId root = 0;
  std::vector<std::uint8_t> encoded;
  const Node *field(NodeId object, std::string_view name) const;
  std::span<const std::uint8_t> blob(const Blob &) const;
};
struct Dataset {
  std::string path;
  std::uint64_t size = 0;
};
struct Reference {
  std::size_t preset = 0;
  NodeId source = 0;
  std::string field, uri;
  // Empty for runtime aliases and unresolved/external references. Never opened.
  std::string resource_dataset;
};
struct ImageSourceStamp {
  std::uint64_t file_bytes = 0;
  std::filesystem::file_time_type modified;
  bool operator==(const ImageSourceStamp &) const = default;
};
struct Archive {
  std::filesystem::path source;
  std::optional<ImageSourceStamp> source_stamp;
  std::vector<Dataset> datasets;
  std::vector<Preset> presets;
  std::vector<Reference> references;
  // Inventory counter only: read_archive reads preset*.bin, not image/package
  // payloads. Includes preset metadata read through H5Dread,
  // excludes HDF5's internal container/index IO.
  std::uint64_t payload_bytes_read = 0;
};
struct ReadOptions {
  // Zero selects half the available RAM via the shared ResourceBudget. A caller
  // may supply a smaller worker budget. There is no fixed preset/count cap.
  std::uint64_t memory_budget_bytes = 0;
};
class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Both functions throw Error on malformed/unsupported framing or unsafe HDF5
// storage. std::bad_alloc may propagate. No resources, scripts or plugins run.
Preset parse_preset(std::span<const std::uint8_t>, ReadOptions = {});
// Concurrent calls serialize HDF5 access and complete handle lifetimes through
// one reader-owned mutex. Error handlers are never changed. Other users of a
// non-thread-safe HDF5 library must coordinate separately with the application.
Archive read_archive(const std::filesystem::path &, ReadOptions = {});

// Lazy, image-only range IO. No generic resource/package payload reader exists.
// Paths must match an inventoried direct /resources/<name>.image dataset. Each
// operation revalidates hard links, byte-array storage, spans and source stamp;
// all HDF5 handles live under the same lock as read_archive.
struct ImageResource {
  std::filesystem::path source;
  Dataset dataset;
  ImageSourceStamp stamp;
};
using ImageBytes = std::vector<std::uint8_t>;
ImageResource describe_image_resource(const Archive &, std::string_view dataset,
                                      ReadOptions = {});
std::shared_ptr<const ImageBytes> read_image_range(const ImageResource &,
    std::uint64_t offset, std::uint64_t count, ReadOptions = {});

// Fixed metadata-only reader for the embedded Shelf previews. Never accepts a
// resource path and cannot open compiled packages. Empty means no preview bank.
std::shared_ptr<const ImageBytes> read_preview_metadata(const std::filesystem::path &,ReadOptions = {});

} // namespace bts::spsm
