// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpsmReader.hh"
#include "../host/platform/ResourceAvailability.hh"

#include <hdf5.h>
#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>
#include <mutex>
#include <set>
#include <type_traits>
#include <utility>

namespace bts::spsm {
namespace {
// Some supported HDF5 distributions are built without thread safety. Keep the
// lock ahead of every reader-owned HDF5 handle, including exceptional unwinding.
// In-memory parse_preset deliberately does not acquire this mutex.
std::mutex hdf5_mutex;

struct SizeAccounting {
  std::uint64_t used = 0;
  explicit SizeAccounting(ReadOptions) {}
  void charge(std::uint64_t n)
  {

    used = resource_sum(used, n);
  }
  void items(std::uint64_t n, std::uint64_t stride)
  {
    if (stride && n > std::numeric_limits<std::size_t>::max() / stride)
      throw Error("SPSM allocation size overflow");
    charge(n * stride);
  }
};

bool utf8(std::string_view s)
{
  for (std::size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80)
      continue;
    unsigned count = 0;
    std::uint32_t code = 0, minimum = 0;
    if (c >= 0xc2 && c <= 0xdf) {
      count = 1;
      code = c & 31;
      minimum = 0x80;
    }
    else if (c >= 0xe0 && c <= 0xef) {
      count = 2;
      code = c & 15;
      minimum = 0x800;
    }
    else if (c >= 0xf0 && c <= 0xf4) {
      count = 3;
      code = c & 7;
      minimum = 0x10000;
    }
    else
      return false;
    if (count > s.size() - i)
      return false;
    for (unsigned j = 0; j < count; ++j) {
      const auto next = static_cast<unsigned char>(s[i++]);
      if ((next & 0xc0) != 0x80)
        return false;
      code = (code << 6) | (next & 63);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
      return false;
  }
  return true;
}

class Parser {
  struct Frame {
    NodeId node;
    std::uint64_t end;
    std::uint32_t remaining;
    bool object;
  };
  SizeAccounting &sizes;
  Preset result;
  std::size_t pos = 0;
  std::uint64_t bound = 0;
  std::vector<Frame> stack;
  [[noreturn]] void fail(const char *message) const
  {
    throw Error("SPSM preset byte " + std::to_string(pos) + ": " + message);
  }
  void need(std::uint64_t n) const
  {
    if (pos > bound || n > bound - pos)
      fail("value crosses its enclosing boundary");
  }
  template<class T> T number()
  {
    static_assert(std::is_unsigned_v<T>);
    need(sizeof(T));
    T n = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i)
      n |= T(result.encoded[pos++]) << (8 * i);
    return n;
  }
  std::string text(bool name = false)
  {
    const auto n = number<std::uint32_t>();
    need(n);
    if (n > std::string{}.max_size())
      fail("string exceeds native representation");
    sizes.charge(std::uint64_t(n) + 1);
    std::string s(reinterpret_cast<const char *>(result.encoded.data() + pos), n);
    pos += n;
    if (!utf8(s) || (name && (s.empty() || s.find('\0') != std::string::npos)))
      fail("invalid UTF-8 or empty/NUL class/field name");
    return s;
  }
  void finish(const Frame &frame)
  {
    if (pos != frame.end)
      fail("container end does not match its declared absolute offset");
    auto &node = result.nodes[frame.node];
    node.end = pos;
    if (auto *object = std::get_if<Object>(&node.value)) {
      sizes.items(object->fields.size(), sizeof(std::string_view));
      std::vector<std::string_view> names;
      names.reserve(object->fields.size());
      for (const auto &field : object->fields)
        names.push_back(field.name);
      std::sort(names.begin(), names.end());
      if (std::adjacent_find(names.begin(), names.end()) != names.end())
        fail("duplicate object field");
    }
  }
  NodeId value()
  {
    Node node;
    node.offset = pos;
    node.wire_tag = number<std::uint8_t>();
    const auto tag = node.wire_tag;
    bool container = false, object = false;
    std::uint64_t end = 0;
    std::uint32_t count = 0;
    if ((tag >= 1 && tag <= 4) || tag == 13 || tag == 14) {
      FloatValues values;
      values.count = tag <= 4 ? tag : (tag == 13 ? 9 : 16);
      for (unsigned i = 0; i < values.count; ++i)
        values.values[i] = std::bit_cast<float>(number<std::uint32_t>());
      node.value = values;  // Preserve IEEE NaN/Inf bits; this is not an evaluator.
    }
    else if (tag >= 5 && tag <= 8) {
      IntValues values;
      values.count = tag - 4;
      for (unsigned i = 0; i < values.count; ++i)
        values.values[i] = std::bit_cast<std::int32_t>(number<std::uint32_t>());
      node.value = values;
    }
    else if (tag == 9)
      node.value = std::bit_cast<std::int64_t>(number<std::uint64_t>());
    else if (tag == 10) {
      const auto flag = number<std::uint8_t>();
      if (flag != 0 && flag != 255)
        fail("unsupported boolean encoding");
      node.value = flag != 0;
    }
    else if (tag == 11)
      node.value = std::uint64_t(number<std::uint32_t>());
    else if (tag == 12 || tag == 21)
      node.value = number<std::uint64_t>();
    else if (tag == 15) {
      const auto n = number<std::uint64_t>();
      need(n);
      node.value = Blob{pos, n};
      pos += static_cast<std::size_t>(n);
    }
    else if (tag == 16)
      node.value = text();
    else if (tag == 17 || tag == 19) {
      end = number<std::uint32_t>();
      if (end < pos || end > bound)
        fail("array absolute end outside parent");
      bound = end;
      count = number<std::uint32_t>();
      if (count > bound - pos)
        fail("array count cannot fit before its end");
      if (count > Array{}.elements.max_size())
        fail("array exceeds native representation");
      sizes.items(count, sizeof(NodeId));
      node.value = Array{};
      std::get<Array>(node.value).elements.reserve(count);
      container = true;
    }
    else if (tag == 18 || tag == 20) {
      const auto null = number<std::uint8_t>();
      if (null != 0 && null != 255)
        fail("unsupported object null marker");
      if (!null) {
        end = number<std::uint32_t>();
        if (end < pos || end > bound)
          fail("object absolute end outside parent");
        bound = end;
        Object object_value;
        object_value.class_name = text(true);
        count = number<std::uint16_t>();
        // Each field needs a length, at least one name byte, and a value tag.
        if (count > (bound - pos) / 6)
          fail("object field count cannot fit before its end");
        if (count > object_value.fields.max_size())
          fail("object exceeds native representation");
        sizes.items(count, sizeof(Field));
        object_value.fields.reserve(count);
        node.value = std::move(object_value);
        container = object = true;
      }
    }
    else
      fail("unknown wire tag; cannot safely skip unframed value");
    node.end = pos;
    // 2x accounts for amortized arena growth, without a fixed node count limit.
    sizes.items(2, sizeof(Node));
    if (result.nodes.size() == result.nodes.max_size())
      fail("node arena exceeds native representation");
    const auto id = result.nodes.size();
    result.nodes.push_back(std::move(node));
    if (container) {
      sizes.items(2, sizeof(Frame));
      stack.push_back({id, end, count, object});
    }
    return id;
  }

 public:
  Parser(std::vector<std::uint8_t> bytes, SizeAccounting &b) : sizes(b)
  {
    result.encoded = std::move(bytes);
  }
  Preset run()
  {
    bound = result.encoded.size();
    if (number<std::uint32_t>() != 0x1b7c2fdd)
      fail("incorrect preset magic");
    result.header_flags = number<std::uint32_t>();
    result.wire_version = number<std::uint32_t>();
    result.root = value();
    while (!stack.empty()) {
      if (!stack.back().remaining) {
        finish(stack.back());
        stack.pop_back();
        continue;
      }
      // value() may grow either vector; never retain references across it.
      const auto parent = stack.back().node;
      bound = stack.back().end;
      const bool object = stack.back().object;
      std::string key;
      if (object)
        key = text(true);
      --stack.back().remaining;
      const auto child = value();
      if (object)
        std::get<Object>(result.nodes[parent].value).fields.push_back({std::move(key), child});
      else
        std::get<Array>(result.nodes[parent].value).elements.push_back(child);
    }
    if (pos != result.encoded.size())
      fail("trailing bytes after root value");
    return std::move(result);
  }
};

class Handle {
  hid_t id_ = -1;
  herr_t (*close_)(hid_t) = nullptr;

 public:
  Handle(hid_t id, herr_t (*close)(hid_t)) : id_(id), close_(close)
  {
    if (id < 0)
      throw Error("HDF5 could not open/inspect SPSM object");
  }
  ~Handle()
  {
    if (id_ >= 0)
      close_(id_);
  }
  Handle(const Handle &) = delete;
  operator hid_t() const
  {
    return id_;
  }
};
void check(herr_t status, const char *operation)
{
  if (status < 0)
    throw Error(std::string("HDF5 SPSM ") + operation + " failed");
}
std::uint64_t dataset_length(hid_t dataset)
{
  Handle type(H5Dget_type(dataset), H5Tclose);
  Handle space(H5Dget_space(dataset), H5Sclose);
  Handle properties(H5Dget_create_plist(dataset), H5Pclose);
  if (H5Tget_class(type) != H5T_INTEGER || H5Tget_size(type) != 1 ||
      H5Tget_sign(type) != H5T_SGN_NONE || H5Tget_precision(type) != 8 ||
      H5Tget_offset(type) != 0 || H5Sget_simple_extent_ndims(space) != 1)
    throw Error("SPSM datasets must be rank-one unsigned 8-bit byte arrays");
  const auto layout = H5Pget_layout(properties);
  if ((layout != H5D_CONTIGUOUS && layout != H5D_CHUNKED && layout != H5D_COMPACT) ||
      H5Pget_nfilters(properties) != 0 || H5Pget_external_count(properties) != 0)
    throw Error("SPSM filtered, virtual or external-storage datasets are unsupported");
  hsize_t length = 0;
  if (H5Sget_simple_extent_dims(space, &length, nullptr) != 1)
    throw Error("Invalid HDF5 dataset extent");
  return length;
}
void image_path(std::string_view path)
{
  constexpr std::string_view prefix = "/resources/";
  if (!path.starts_with(prefix))
    throw Error("Image IO requires /resources/<name>.image");
  const auto name = path.substr(prefix.size());
  if (name.size() <= 6 || !name.ends_with(".image") ||
      name.find_first_of("/\\") != std::string_view::npos ||
      name.find('\0') != std::string_view::npos)
    throw Error("Image IO rejects non-image, nested or unsafe resource paths");
}
ImageSourceStamp source_stamp(const std::filesystem::path &path)
{
  return {std::filesystem::file_size(path), std::filesystem::last_write_time(path)};
}
void image_links(hid_t file, const std::string &path)
{
  for (const auto &component : {std::string("/resources"), path}) {
    H5L_info1_t link{};
    check(H5Lget_info1(file, component.c_str(), &link, H5P_DEFAULT), "image link info");
    if (link.type != H5L_TYPE_HARD)
      throw Error("Image IO rejects non-hard links");
    H5O_info1_t object{};
    check(H5Oget_info_by_name1(file, component.c_str(), &object, H5P_DEFAULT),
          "image object info");
    if (object.rc != 1 ||
        object.type != (component == "/resources" ? H5O_TYPE_GROUP : H5O_TYPE_DATASET))
      throw Error("Image IO rejects aliases or invalid resource object types");
  }
}
ReadOptions image_options(ReadOptions)
{
  return {};
}

bool preset_name(std::string_view path)
{
  if (path == "/preset.bin")
    return true;
  constexpr std::string_view prefix = "/preset_", suffix = ".bin";
  if (!path.starts_with(prefix) || !path.ends_with(suffix))
    return false;
  const auto number = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
  return !number.empty() &&
         std::all_of(number.begin(), number.end(), [](char c) { return c >= '0' && c <= '9'; });
}
std::string version_resource(std::string_view uri)
{
  const auto query = uri.find('?');
  if (query == std::string_view::npos)
    return {};
  auto tail = uri.substr(query + 1);
  std::string found;
  while (!tail.empty()) {
    const auto amp = tail.find('&');
    const auto part = tail.substr(0, amp);
    if (part.starts_with("version=")) {
      if (!found.empty())
        return {};  // Ambiguous query; preserve unresolved.
      const auto name = part.substr(8);
      // Observed version keys are opaque ASCII file names. Never interpret a
      // URI as a filesystem path or follow its authoring-machine origin URL.
      if (name.empty() || name == "." || name == ".." ||
          !std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   c == '.' || c == '_' || c == '-';
          }))
        return {};
      found = "/resources/" + std::string(name);
    }
    if (amp == std::string_view::npos)
      break;
    tail.remove_prefix(amp + 1);
  }
  return found;
}
}  // namespace

const Node *Preset::field(NodeId id, std::string_view name) const
{
  if (id >= nodes.size())
    return nullptr;
  if (const auto *object = std::get_if<Object>(&nodes[id].value))
    for (const auto &field : object->fields)
      if (field.name == name)
        return field.value < nodes.size() ? &nodes[field.value] : nullptr;
  return nullptr;
}
std::span<const std::uint8_t> Preset::blob(const Blob &blob) const
{
  if (blob.offset > encoded.size() || blob.size > encoded.size() - blob.offset)
    throw Error("SPSM blob span outside its source stream");
  return std::span(encoded).subspan(static_cast<std::size_t>(blob.offset),
                                    static_cast<std::size_t>(blob.size));
}
Preset parse_preset(std::span<const std::uint8_t> bytes, ReadOptions options)
{
  SizeAccounting sizes(options);
  sizes.charge(bytes.size());
  return Parser(std::vector<std::uint8_t>(bytes.begin(), bytes.end()), sizes).run();
}

Archive read_archive(const std::filesystem::path &path, ReadOptions options)
{
  {
    std::ifstream stream(path, std::ios::binary);
    std::array<unsigned char, 8> magic{};
    stream.read(reinterpret_cast<char *>(magic.data()), magic.size());
    if (!stream || magic != std::array<unsigned char, 8>{0x89, 'H', 'D', 'F', 13, 10, 26, 10})
      throw Error("SPSM file does not begin with the HDF5 signature");
  }
  const std::lock_guard hdf5_lock(hdf5_mutex);
  SizeAccounting sizes(
      options);  // Check representation-size arithmetic after waiting for another reader.
  Handle file(H5Fopen(path.string().c_str(), H5F_ACC_RDONLY, H5P_DEFAULT), H5Fclose);
  Archive result;
  sizes.items(path.native().size(), sizeof(std::filesystem::path::value_type));
  result.source = path;
  result.source_stamp = source_stamp(path);
  sizes.charge(128);
  std::vector<std::string> groups{"/"};
  std::set<haddr_t> seen;
  H5O_info1_t root_info{};
  check(H5Oget_info_by_name1(file, "/", &root_info, H5P_DEFAULT), "root info");
  seen.insert(root_info.addr);
  // Iterative traversal rejects aliases/cycles as well as external/soft links.
  for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
    const auto group_path = groups[group_index];
    Handle group(H5Gopen2(file, group_path.c_str(), H5P_DEFAULT), H5Gclose);
    H5G_info_t info{};
    check(H5Gget_info(group, &info), "group info");
    for (hsize_t i = 0; i < info.nlinks; ++i) {
      H5L_info1_t link{};
      check(H5Lget_info_by_idx1(group, ".", H5_INDEX_NAME, H5_ITER_INC, i, &link, H5P_DEFAULT),
            "link info");
      if (link.type != H5L_TYPE_HARD)
        throw Error("SPSM external, soft or user-defined links are unsupported");
      const auto size = H5Lget_name_by_idx(
          group, ".", H5_INDEX_NAME, H5_ITER_INC, i, nullptr, 0, H5P_DEFAULT);
      if (size < 0 || std::uint64_t(size) >= std::string{}.max_size())
        throw Error("Invalid HDF5 link name length");
      sizes.items(std::uint64_t(size), 5);
      sizes.items(group_path.size(), 4);
      sizes.charge(128);
      std::string name(static_cast<std::size_t>(size) + 1, '\0');
      const auto actual = H5Lget_name_by_idx(
          group, ".", H5_INDEX_NAME, H5_ITER_INC, i, name.data(), name.size(), H5P_DEFAULT);
      if (actual != size)
        throw Error("HDF5 link changed during SPSM read");
      name.resize(static_cast<std::size_t>(size));
      if (!utf8(name))
        throw Error("SPSM dataset name is not UTF-8");
      const auto full = (group_path == "/" ? "/" : group_path + "/") + name;
      H5O_info1_t object{};
      check(H5Oget_info_by_name1(file, full.c_str(), &object, H5P_DEFAULT), "object info");
      if (!seen.insert(object.addr).second)
        throw Error("SPSM hard-link aliases/cycles are unsupported");
      if (object.type == H5O_TYPE_GROUP) {
        sizes.items(2, sizeof(std::string));
        groups.push_back(full);
        continue;
      }
      if (object.type != H5O_TYPE_DATASET)
        throw Error("SPSM contains an unsupported HDF5 object type");
      Handle dataset(H5Dopen2(file, full.c_str(), H5P_DEFAULT), H5Dclose);
      const auto length = dataset_length(dataset);
      sizes.items(2, sizeof(Dataset));
      result.datasets.push_back({full, length});
      if (!preset_name(full))
        continue;  // No H5Dread for packages/images/previews.
      if (length > std::numeric_limits<std::uint32_t>::max() ||
          length > std::numeric_limits<std::size_t>::max())
        throw Error("SPSM preset exceeds its absolute uint32 wire offsets");
      sizes.charge(length);
      std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
      if (length)
        check(H5Dread(dataset, H5T_NATIVE_UCHAR, H5S_ALL, H5S_ALL, H5P_DEFAULT, bytes.data()),
              "preset read");
      result.payload_bytes_read = resource_sum(result.payload_bytes_read, length);
      Preset preset;
      try {
        preset = Parser(std::move(bytes), sizes).run();
      }
      catch (const Error &error) {
        throw Error(full + ": " + error.what());
      }
      preset.dataset = full;
      const auto *root = std::get_if<Object>(&preset.nodes[preset.root].value);
      if (!root || (root->class_name != "DataLayersPreset" && root->class_name != "DataSmartMask"))
        throw Error("SPSM root is not DataLayersPreset or DataSmartMask");
      sizes.items(2, sizeof(Preset));
      result.presets.push_back(std::move(preset));
    }
  }
  if (std::none_of(result.presets.begin(), result.presets.end(), [](const Preset &p) {
        return p.dataset == "/preset.bin";
      }))
    throw Error("SPSM archive has no /preset.bin");
  for (std::size_t pi = 0; pi < result.presets.size(); ++pi) {
    const auto &preset = result.presets[pi];
    for (const auto &node : preset.nodes)
      if (const auto *object = std::get_if<Object>(&node.value)) {
        for (const auto &field : object->fields) {
          if (!((object->class_name == "DataProcedural" && field.name == "urlToSbsRes") ||
                (object->class_name == "DataBitmap" && field.name == "urlToBitmapRes")))
            continue;
          const auto *uri = std::get_if<std::string>(&preset.nodes[field.value].value);
          if (!uri)
            throw Error("SPSM resource URL field is not a string");
          sizes.items(uri->size(), 2);
          sizes.charge(field.name.size() + 12);
          sizes.items(2, sizeof(Reference));
          auto resource = version_resource(*uri);
          if (std::none_of(result.datasets.begin(), result.datasets.end(), [&](const Dataset &d) {
                return d.path == resource;
              }))
            resource.clear();
          result.references.push_back({pi, field.value, field.name, *uri, std::move(resource)});
        }
      }
  }
  if (source_stamp(path) != *result.source_stamp)
    throw Error("SPSM source changed during inventory");
  return result;
}
ImageResource describe_image_resource(const Archive &archive,
                                      std::string_view path,
                                      ReadOptions options)
{
  image_path(path);
  const auto found = std::find_if(archive.datasets.begin(),
                                  archive.datasets.end(),
                                  [&](const Dataset &d) { return d.path == path; });
  if (found == archive.datasets.end())
    throw Error("Image is not in the inventoried resource bank");
  const std::lock_guard hdf5_lock(hdf5_mutex);
  SizeAccounting sizes(image_options(options));
  sizes.items(path.size(), 2);
  sizes.charge(sizeof(ImageResource));
  sizes.items(archive.source.native().size(), 2 * sizeof(std::filesystem::path::value_type));
  const auto stamp = source_stamp(archive.source);
  if (archive.source_stamp && *archive.source_stamp != stamp)
    throw Error("SPSM source changed since inventory");
  Handle file(H5Fopen(archive.source.string().c_str(), H5F_ACC_RDONLY, H5P_DEFAULT), H5Fclose);
  image_links(file, found->path);
  Handle dataset(H5Dopen2(file, found->path.c_str(), H5P_DEFAULT), H5Dclose);
  if (dataset_length(dataset) != found->size || source_stamp(archive.source) != stamp)
    throw Error("SPSM image source changed since inventory");
  return {archive.source, *found, stamp};
}
std::shared_ptr<const ImageBytes> read_image_range(const ImageResource &resource,
                                                   std::uint64_t offset,
                                                   std::uint64_t count,
                                                   ReadOptions options)
{
  image_path(resource.dataset.path);
  if (offset > resource.dataset.size || count > resource.dataset.size - offset)
    throw Error("Image read span crosses dataset bounds");
  if (count > ImageBytes{}.max_size() || count > std::numeric_limits<hsize_t>::max())
    throw Error("Image read exceeds native/HDF5 span representation");
  const std::lock_guard hdf5_lock(hdf5_mutex);
  SizeAccounting sizes(image_options(options));
  sizes.charge(count);
  sizes.charge(sizeof(ImageBytes) + 128);
  sizes.items(resource.source.native().size(), 2 * sizeof(std::filesystem::path::value_type));
  if (source_stamp(resource.source) != resource.stamp)
    throw Error("SPSM image source changed before read");
  Handle file(H5Fopen(resource.source.string().c_str(), H5F_ACC_RDONLY, H5P_DEFAULT), H5Fclose);
  image_links(file, resource.dataset.path);
  Handle dataset(H5Dopen2(file, resource.dataset.path.c_str(), H5P_DEFAULT), H5Dclose);
  if (dataset_length(dataset) != resource.dataset.size)
    throw Error("SPSM image dataset extent changed");
  auto result = std::make_shared<ImageBytes>(static_cast<std::size_t>(count));
  if (count) {
    Handle space(H5Dget_space(dataset), H5Sclose);
    const hsize_t start = offset, length = count;
    check(H5Sselect_hyperslab(space, H5S_SELECT_SET, &start, nullptr, &length, nullptr),
          "image read span");
    Handle memory(H5Screate_simple(1, &length, nullptr), H5Sclose);
    check(H5Dread(dataset, H5T_NATIVE_UCHAR, memory, space, H5P_DEFAULT, result->data()),
          "image read");
  }
  if (source_stamp(resource.source) != resource.stamp)
    throw Error("SPSM image source changed during read");
  return result;
}
std::shared_ptr<const ImageBytes> read_preview_metadata(const std::filesystem::path &path,
                                                        ReadOptions options)
{
  const std::lock_guard hdf5_lock(hdf5_mutex);
  SizeAccounting sizes(image_options(options));
  const auto before = source_stamp(path);
  Handle file(H5Fopen(path.string().c_str(), H5F_ACC_RDONLY, H5P_DEFAULT), H5Fclose);
  if (H5Lexists(file, "/.alg_meta", H5P_DEFAULT) <= 0 ||
      H5Lexists(file, "/.alg_meta/preset.bin", H5P_DEFAULT) <= 0)
    return {};
  for (const auto component : {"/.alg_meta", "/.alg_meta/preset.bin"}) {
    H5L_info1_t link{};
    check(H5Lget_info1(file, component, &link, H5P_DEFAULT), "preview link info");
    H5O_info1_t object{};
    check(H5Oget_info_by_name1(file, component, &object, H5P_DEFAULT), "preview object info");
    if (link.type != H5L_TYPE_HARD || object.rc != 1 ||
        object.type !=
            (std::string_view(component) == "/.alg_meta" ? H5O_TYPE_GROUP : H5O_TYPE_DATASET))
      throw Error("SPSM preview rejects links or invalid metadata objects");
  }
  Handle dataset(H5Dopen2(file, "/.alg_meta/preset.bin", H5P_DEFAULT), H5Dclose);
  const auto length = dataset_length(dataset);
  if (length > ImageBytes{}.max_size())
    throw Error("SPSM preview metadata exceeds native storage");
  sizes.charge(resource_sum(length, sizeof(ImageBytes) + 128));
  auto result = std::make_shared<ImageBytes>(std::size_t(length));
  if (length)
    check(H5Dread(dataset, H5T_NATIVE_UCHAR, H5S_ALL, H5S_ALL, H5P_DEFAULT, result->data()),
          "preview metadata read");
  if (source_stamp(path) != before)
    throw Error("SPSM preview source changed during read");
  return result;
}
}  // namespace bts::spsm
