// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpsmReader.hh"
#include "../host/ResourceAvailability.hh"
#include <hdf5.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <latch>
#include <limits>
#include <map>
#include <set>
#include <thread>
#include <type_traits>

using namespace bts::spsm;
using namespace std::string_view_literals;
namespace {
void require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
struct Writer {
  std::vector<std::uint8_t> bytes;
  Writer()
  {
    put<std::uint32_t>(0x1b7c2fdd);
    put<std::uint32_t>(0);
    put<std::uint32_t>(45);
  }
  template<class T> void put(T value)
  {
    for (std::size_t i = 0; i < sizeof(T); ++i)
      bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
  }
  void tag(unsigned value)
  {
    put<std::uint8_t>(value);
  }
  void text(std::string_view text)
  {
    put<std::uint32_t>(static_cast<std::uint32_t>(text.size()));
    bytes.insert(bytes.end(), text.begin(), text.end());
  }
  std::size_t object(std::string_view name, unsigned fields, unsigned tag_value = 18)
  {
    tag(tag_value);
    tag(0);
    const auto end = bytes.size();
    put<std::uint32_t>(0);
    text(name);
    put<std::uint16_t>(fields);
    return end;
  }
  std::size_t array(unsigned count, unsigned tag_value = 17)
  {
    tag(tag_value);
    const auto end = bytes.size();
    put<std::uint32_t>(0);
    put<std::uint32_t>(count);
    return end;
  }
  void patch(std::size_t offset, std::uint32_t value)
  {
    for (unsigned i = 0; i < 4; ++i)
      bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (i * 8));
  }
  void end(std::size_t offset)
  {
    patch(offset, static_cast<std::uint32_t>(bytes.size()));
  }
};
template<class F> void rejects(F operation, std::string_view reason = {})
{
  try {
    operation();
  }
  catch (const Error &error) {
    require(reason.empty() ||
                std::string_view(error.what()).find(reason) != std::string_view::npos,
            "rejected input for an unexpected reason");
    return;
  }
  throw std::runtime_error("malformed/unsupported input was accepted");
}
Writer sample()
{
  Writer writer;
  const auto root = writer.object("DataLayersPreset", 1);
  writer.text("allTypes");
  const auto array = writer.array(23, 19);
  for (unsigned tag = 1; tag <= 4; ++tag) {
    writer.tag(tag);
    for (unsigned i = 0; i < tag; ++i)
      writer.put<std::uint32_t>(std::bit_cast<std::uint32_t>(float(i) - 1.5f));
  }
  for (unsigned tag = 5; tag <= 8; ++tag) {
    writer.tag(tag);
    for (unsigned i = 0; i < tag - 4; ++i)
      writer.put<std::uint32_t>(0xffffffff);
  }
  writer.tag(9);
  writer.put<std::uint64_t>(0x8000000000000000ull);
  writer.tag(10);
  writer.tag(255);
  writer.tag(11);
  writer.put<std::uint32_t>(0xffffffff);
  writer.tag(12);
  writer.put<std::uint64_t>(0xffffffffffffffffull);
  for (unsigned tag : {13, 14}) {
    writer.tag(tag);
    for (unsigned i = 0; i < (tag == 13 ? 9 : 16); ++i)
      writer.put<std::uint32_t>(i);
  }
  writer.tag(15);
  writer.put<std::uint64_t>(3);
  writer.tag(0);
  writer.tag(128);
  writer.tag(255);
  writer.tag(16);
  writer.text("trä\0x"sv);
  const auto empty_array = writer.array(0);
  writer.end(empty_array);
  writer.tag(18);
  writer.tag(255);
  const auto unknown = writer.object("FutureClass", 1, 20);
  writer.text("futureField");
  writer.tag(16);
  writer.text("preserved");
  writer.end(unknown);
  writer.tag(21);
  writer.put<std::uint64_t>(0xffffffffffffffffull);
  writer.tag(1);
  writer.put<std::uint32_t>(0x7fc01234);  // Preserve NaN payload.
  writer.tag(10);
  writer.tag(0);
  writer.tag(20);
  writer.tag(255);
  writer.end(array);
  writer.end(root);
  return writer;
}
void parser_tests()
{
  auto writer = sample();
  const auto preset = parse_preset(writer.bytes);
  require(preset.wire_version == 45 && preset.nodes.size() == 26,
          "typed arena size/header mismatch");
  const auto *field = preset.field(preset.root, "allTypes");
  require(field, "root field missing");
  const auto &array = std::get<Array>(field->value);
  require(array.elements.size() == 23, "array count mismatch");
  require(std::get<std::int64_t>(preset.nodes[array.elements[8]].value) ==
              std::numeric_limits<std::int64_t>::min(),
          "signed int64 lost");
  require(std::get<std::uint64_t>(preset.nodes[array.elements[11]].value) == UINT64_MAX,
          "unsigned int64 lost");
  const auto blob = std::get<Blob>(preset.nodes[array.elements[14]].value);
  require(preset.blob(blob).size() == 3 && preset.blob(blob)[2] == 255, "blob source span lost");
  require(std::get<std::string>(preset.nodes[array.elements[15]].value).size() == 6,
          "UTF-8/NUL string lost");
  require(std::get<Object>(preset.nodes[array.elements[18]].value).class_name == "FutureClass",
          "unknown semantic class not preserved");
  require(std::bit_cast<std::uint32_t>(
              std::get<FloatValues>(preset.nodes[array.elements[20]].value).values[0]) ==
              0x7fc01234,
          "float payload changed");
  rejects([&] { preset.blob({UINT64_MAX, 1}); });
  for (std::size_t i = 0; i < writer.bytes.size(); ++i)
    rejects([&] { parse_preset(std::span(writer.bytes).first(i)); });
  require(!parse_preset(writer.bytes, {}).nodes.empty(),
          "Valid source rejected by former configured limit");
  {
    bts::TextureRamQueryScope ram([]() -> std::uint64_t { return 32; });
    require(!parse_preset(writer.bytes).nodes.empty(), "Valid source rejected by RAM telemetry");
  }
  auto corrupted = writer.bytes;
  corrupted.push_back(0);
  rejects([&] { parse_preset(corrupted); }, "trailing");
  corrupted = writer.bytes;
  corrupted[12] = 255;
  rejects([&] { parse_preset(corrupted); }, "unknown wire tag");
  corrupted = writer.bytes;
  corrupted[13] = 1;
  rejects([&] { parse_preset(corrupted); }, "null marker");
  corrupted = writer.bytes;
  corrupted[14] = 0;
  corrupted[15] = 0;
  corrupted[16] = 0;
  corrupted[17] = 0;
  rejects([&] { parse_preset(corrupted); }, "absolute end");
  Writer huge;
  const auto end = huge.array(UINT32_MAX);
  huge.end(end);
  rejects([&] { parse_preset(huge.bytes); }, "array count");
  Writer huge_blob;
  huge_blob.tag(15);
  huge_blob.put<std::uint64_t>(UINT64_MAX);
  rejects([&] { parse_preset(huge_blob.bytes); }, "boundary");
  Writer huge_text;
  huge_text.tag(16);
  huge_text.put<std::uint32_t>(UINT32_MAX);
  rejects([&] { parse_preset(huge_text.bytes); }, "boundary");
  Writer huge_fields;
  const auto fields_end = huge_fields.object("Future", UINT16_MAX);
  huge_fields.end(fields_end);
  rejects([&] { parse_preset(huge_fields.bytes); }, "field count");
  Writer huge_end;
  const auto impossible_end = huge_end.array(0);
  huge_end.patch(impossible_end, UINT32_MAX);
  rejects([&] { parse_preset(huge_end.bytes); }, "outside parent");
  Writer boolean;
  boolean.tag(10);
  boolean.tag(1);
  rejects([&] { parse_preset(boolean.bytes); }, "boolean");
  for (const std::string &invalid : {std::string("\xc0\x80", 2),
                                     std::string("\xed\xa0\x80", 3),
                                     std::string("\xf4\x90\x80\x80", 4),
                                     std::string("\xc2", 1)})
  {
    Writer text;
    text.tag(16);
    text.text(invalid);
    rejects([&] { parse_preset(text.bytes); }, "UTF-8");
  }
  Writer duplicate;
  const auto object = duplicate.object("Future", 2);
  for (unsigned i = 0; i < 2; ++i) {
    duplicate.text("a");
    duplicate.tag(18);
    duplicate.tag(255);
  }
  duplicate.end(object);
  rejects([&] { parse_preset(duplicate.bytes); }, "duplicate");
  // Child announces bytes outside the parent but inside the physical stream.
  Writer escaped;
  const auto outer = escaped.array(1);
  const auto inner = escaped.array(0);
  escaped.end(outer);
  escaped.tag(0);
  escaped.end(inner);
  rejects([&] { parse_preset(escaped.bytes); }, "outside parent");
  // Deterministic byte mutations exercise malformed combinations under a small
  // worker budget. Structurally valid value mutations may remain valid.
  std::uint32_t random = 0x5a17cafe;
  for (unsigned i = 0; i < 4000; ++i) {
    auto mutated = writer.bytes;
    random = random * 1664525 + 1013904223;
    const auto at = random % mutated.size();
    random = random * 1664525 + 1013904223;
    mutated[at] = random >> 24;
    try {
      const auto parsed = parse_preset(mutated);
      for (const auto &node : parsed.nodes)
        require(node.offset < node.end && node.end <= mutated.size(),
                "mutation escaped source span");
    }
    catch (const Error &) {
    }
  }
  // No arbitrary nesting cap; arena and parser/destructor are iterative.
  Writer deep;
  std::vector<std::size_t> ends;
  for (unsigned i = 0; i < 20000; ++i)
    ends.push_back(deep.array(1));
  deep.tag(18);
  deep.tag(255);
  for (auto it = ends.rbegin(); it != ends.rend(); ++it)
    deep.end(*it);
  require(parse_preset(deep.bytes).nodes.size() == 20001, "deep iterative tree failed");
  std::cout
      << "PASS typed IR: all tags, every truncation, bounds, UTF-8, duplicates, budget, 4000 mutations, 20000 nested containers\n";
}
struct Temporary {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("bts-spsm-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  Temporary()
  {
    require(std::filesystem::create_directory(path), "cannot create private test directory");
  }
  ~Temporary()
  {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};
void hcheck(herr_t value)
{
  require(value >= 0, "HDF5 test setup failed");
}
void write_preset(hid_t file, const std::vector<std::uint8_t> &bytes)
{
  hsize_t size = bytes.size();
  const auto space = H5Screate_simple(1, &size, nullptr);
  const auto dataset = H5Dcreate2(
      file, "/preset.bin", H5T_STD_U8LE, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  require(dataset >= 0, "test preset create failed");
  hcheck(H5Dwrite(dataset, H5T_NATIVE_UCHAR, H5S_ALL, H5S_ALL, H5P_DEFAULT, bytes.data()));
  hcheck(H5Dclose(dataset));
  hcheck(H5Sclose(space));
}
void create_lazy_archive(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes)
{
  const auto file = H5Fcreate(path.string().c_str(), H5F_ACC_EXCL, H5P_DEFAULT, H5P_DEFAULT);
  require(file >= 0, "test file create failed");
  write_preset(file, bytes);
  auto group = H5Gcreate2(file, "/resources", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  hcheck(H5Gclose(group));
  hsize_t size = hsize_t(1) << 34, chunk = 65536;
  auto space = H5Screate_simple(1, &size, nullptr);
  auto properties = H5Pcreate(H5P_DATASET_CREATE);
  hcheck(H5Pset_chunk(properties, 1, &chunk));
  auto dataset = H5Dcreate2(
      file, "/resources/opaque.image", H5T_STD_U8LE, space, H5P_DEFAULT, properties, H5P_DEFAULT);
  require(dataset >= 0, "lazy image create failed");
  hcheck(H5Dclose(dataset));
  hcheck(H5Pclose(properties));
  hcheck(H5Sclose(space));
  hcheck(H5Fclose(file));
}
void hdf_tests()
{
  Temporary temp;
  const auto good = temp.path / "good.spsm";
  const auto bytes = sample().bytes;
  create_lazy_archive(good, bytes);
  const auto archive = read_archive(good);
  require(archive.presets.size() == 1 && archive.datasets.size() == 2 &&
              archive.payload_bytes_read == bytes.size(),
          "lazy resource read policy failed");
  require(archive.datasets.back().size == (std::uint64_t(1) << 34),
          "64-bit resource descriptor lost");
  read_archive(good, {});
  auto mutate = [&](std::string_view name, auto setup, std::string_view reason) {
    const auto path = temp.path / (std::string(name) + ".spsm");
    const auto file = H5Fcreate(path.string().c_str(), H5F_ACC_EXCL, H5P_DEFAULT, H5P_DEFAULT);
    write_preset(file, bytes);
    setup(file);
    hcheck(H5Fclose(file));
    rejects([&] { read_archive(path); }, reason);
  };
  mutate(
      "soft",
      [&](hid_t f) {
        hcheck(H5Lcreate_soft("/preset.bin", f, "/link", H5P_DEFAULT, H5P_DEFAULT));
      },
      "links");
  mutate(
      "external-link",
      [&](hid_t f) {
        hcheck(
            H5Lcreate_external("/must-not-open.spsm", "/x", f, "/link", H5P_DEFAULT, H5P_DEFAULT));
      },
      "links");
  mutate(
      "cycle",
      [&](hid_t f) { hcheck(H5Lcreate_hard(f, "/", f, "/cycle", H5P_DEFAULT, H5P_DEFAULT)); },
      "aliases/cycles");
  auto bad_dataset = [&](std::string_view name, hid_t type, int rank, int storage) {
    mutate(
        name,
        [&](hid_t f) {
          hsize_t dims[2]{16, 16};
          const auto s = H5Screate_simple(rank, dims, nullptr);
          const auto p = H5Pcreate(H5P_DATASET_CREATE);
          if (storage == 1) {
            hsize_t c = 16;
            hcheck(H5Pset_chunk(p, 1, &c));
            hcheck(H5Pset_deflate(p, 1));
          }
          if (storage == 2)
            hcheck(H5Pset_external(p, "/must-not-open.raw", 0, H5F_UNLIMITED));
          if (storage == 3)
            hcheck(H5Pset_virtual(p, s, "/must-not-open.h5", "/pixels", s));
          const auto d = H5Dcreate2(f, "/bad", type, s, H5P_DEFAULT, p, H5P_DEFAULT);
          require(d >= 0, "bad dataset setup failed");
          hcheck(H5Dclose(d));
          hcheck(H5Pclose(p));
          hcheck(H5Sclose(s));
        },
        storage ? "storage datasets" : "byte arrays");
  };
  bad_dataset("signed", H5T_STD_I8LE, 1, 0);
  bad_dataset("wide", H5T_STD_U16LE, 1, 0);
  bad_dataset("rank", H5T_STD_U8LE, 2, 0);
  bad_dataset("filtered", H5T_STD_U8LE, 1, 1);
  bad_dataset("external-storage", H5T_STD_U8LE, 1, 2);
  bad_dataset("virtual", H5T_STD_U8LE, 1, 3);
  const auto not_hdf = temp.path / "fake.spsm";
  {
    std::ofstream stream(not_hdf);
    stream << "not HDF5";
  }
  rejects([&] { read_archive(not_hdf); }, "signature");
  std::cout
      << "PASS HDF5: read-only metadata, 16GiB lazy image, link/cycle/type/rank/filter/external/virtual rejection\n";
}
bool same_node(const Node &a, const Node &b)
{
  if (a.wire_tag != b.wire_tag || a.offset != b.offset || a.end != b.end ||
      a.value.index() != b.value.index())
    return false;
  return std::visit(
      [&]<class T>(const T &left) {
        const auto &right = std::get<T>(b.value);
        if constexpr (std::is_same_v<T, std::monostate>)
          return true;
        else if constexpr (std::is_same_v<T, FloatValues>) {
          if (left.count != right.count)
            return false;
          for (unsigned i = 0; i < left.count; ++i)
            if (std::bit_cast<std::uint32_t>(left.values[i]) !=
                std::bit_cast<std::uint32_t>(right.values[i]))
              return false;
          return true;
        }
        else if constexpr (std::is_same_v<T, IntValues>)
          return left.count == right.count && left.values == right.values;
        else if constexpr (std::is_same_v<T, Blob>)
          return left.offset == right.offset && left.size == right.size;
        else if constexpr (std::is_same_v<T, Array>)
          return left.elements == right.elements;
        else if constexpr (std::is_same_v<T, Object>) {
          if (left.class_name != right.class_name || left.fields.size() != right.fields.size())
            return false;
          for (std::size_t i = 0; i < left.fields.size(); ++i)
            if (left.fields[i].name != right.fields[i].name ||
                left.fields[i].value != right.fields[i].value)
              return false;
          return true;
        }
        else
          return left == right;
      },
      a.value);
}
void same_preset(const Preset &a, const Preset &b)
{
  require(a.header_flags == b.header_flags && a.wire_version == b.wire_version &&
              a.root == b.root && a.nodes.size() == b.nodes.size() && a.encoded == b.encoded,
          "parallel preset header/encoded bytes changed");
  for (std::size_t i = 0; i < a.nodes.size(); ++i)
    require(same_node(a.nodes[i], b.nodes[i]), "parallel typed metadata changed");
}
void same_archive(const Archive &a, const Archive &b)
{
  require(a.source == b.source && a.datasets.size() == b.datasets.size() &&
              a.presets.size() == b.presets.size() && a.references.size() == b.references.size() &&
              a.payload_bytes_read == b.payload_bytes_read,
          "parallel archive inventory changed");
  std::uint64_t metadata = 0;
  for (std::size_t i = 0; i < a.datasets.size(); ++i)
    require(a.datasets[i].path == b.datasets[i].path && a.datasets[i].size == b.datasets[i].size,
            "parallel dataset changed");
  for (std::size_t i = 0; i < a.presets.size(); ++i) {
    require(a.presets[i].dataset == b.presets[i].dataset, "parallel preset dataset changed");
    same_preset(a.presets[i], b.presets[i]);
    metadata += a.presets[i].encoded.size();
  }
  require(a.payload_bytes_read == metadata, "parallel reader hydrated a resource payload");
  for (std::size_t i = 0; i < a.references.size(); ++i) {
    const auto &x = a.references[i], &y = b.references[i];
    require(x.preset == y.preset && x.source == y.source && x.field == y.field && x.uri == y.uri &&
                x.resource_dataset == y.resource_dataset,
            "parallel resource binding changed");
  }
}
void parallel_tests(const std::filesystem::path &directory = {})
{
  Temporary temp;
  const auto good = temp.path / "good.spsm", bad = temp.path / "bad.spsm",
             corrupt = temp.path / "corrupt.spsm";
  const auto bytes = sample().bytes;
  create_lazy_archive(good, bytes);
  auto broken = bytes;
  broken.pop_back();
  create_lazy_archive(bad, broken);
  {  // Correct signature, invalid HDF5 superblock: exercises an actual HDF5 failure.
    std::ofstream stream(corrupt, std::ios::binary);
    const unsigned char magic[]{0x89, 'H', 'D', 'F', 13, 10, 26, 10};
    stream.write(reinterpret_cast<const char *>(magic), sizeof(magic));
  }
  std::vector<std::filesystem::path> paths{good};
  if (!directory.empty())
    for (const auto *name : {"dirty-plastic-robot.spsm",
                             "shd-hard-rust.spsm",
                             "sratched-metal-pealing-paint-dirt.spsm",
                             "used_varnish_wood.spsm",
                             "worn leather chest.spsm"})
      paths.push_back(directory / name);
  std::vector<Archive> baseline;
  std::vector<std::filesystem::file_time_type> times;
  for (const auto &path : paths) {
    times.push_back(std::filesystem::last_write_time(path));
    baseline.push_back(read_archive(path));
  }
  const auto parsed = parse_preset(bytes);
  std::atomic<unsigned> callback_count{0};
  struct RestoreErrorHandler {
    H5E_auto2_t callback = nullptr;
    void *client = nullptr;
    RestoreErrorHandler()
    {
      hcheck(H5Eget_auto2(H5E_DEFAULT, &callback, &client));
    }
    ~RestoreErrorHandler()
    {
      H5Eset_auto2(H5E_DEFAULT, callback, client);
    }
  } restore_handler;
  hbool_t thread_safe = false;
  hcheck(H5is_library_threadsafe(&thread_safe));
  if (!thread_safe) {
    // A non-null process handler detects readers that silently suppress errors,
    // even if they later restore it. No direct test HDF5 calls run in parallel.
    hcheck(H5Eset_auto2(
        H5E_DEFAULT,
        +[](hid_t, void *client) -> herr_t {
          static_cast<std::atomic<unsigned> *>(client)->fetch_add(1, std::memory_order_relaxed);
          return 0;
        },
        &callback_count));
  }
  H5E_auto2_t callback_before = nullptr;
  void *client_before = nullptr;
  hcheck(H5Eget_auto2(H5E_DEFAULT, &callback_before, &client_before));
  const auto handles_before = H5Fget_obj_count(H5F_OBJ_ALL, H5F_OBJ_ALL);
  require(handles_before >= 0, "HDF5 handle count failed");
  std::vector<std::function<void()>> jobs;
  for (std::size_t i = 0; i < paths.size(); ++i)
    jobs.push_back([&, i] {
      for (unsigned round = 0; round < 8; ++round)
        same_archive(read_archive(paths[i]), baseline[i]);
    });
  jobs.push_back([&] {
    for (unsigned round = 0; round < 8; ++round)
      rejects([&] { read_archive(bad); }, "/preset.bin");
  });
  jobs.push_back([&] {
    for (unsigned round = 0; round < 8; ++round)
      rejects([&] { read_archive(corrupt); }, "HDF5");
  });
  for (unsigned i = 0; i < 2; ++i)
    jobs.push_back([&] {
      for (unsigned round = 0; round < 64; ++round)
        same_preset(parse_preset(bytes), parsed);
    });
  std::latch start(1);
  std::vector<std::exception_ptr> errors(jobs.size());
  std::vector<std::jthread> workers;
  try {
    for (std::size_t i = 0; i < jobs.size(); ++i)
      workers.emplace_back([&, i] {
        start.wait();
        try {
          jobs[i]();
        }
        catch (...) {
          errors[i] = std::current_exception();
        }
      });
  }
  catch (...) {
    start.count_down();
    throw;
  }  // Release existing workers if spawning fails.
  start.count_down();
  workers.clear();  // Join before inspecting results or HDF5 state.
  for (const auto &error : errors)
    if (error)
      std::rethrow_exception(error);
  H5E_auto2_t callback_after = nullptr;
  void *client_after = nullptr;
  hcheck(H5Eget_auto2(H5E_DEFAULT, &callback_after, &client_after));
  require(callback_after == callback_before && client_after == client_before,
          "reader changed HDF5 error handler");
  if (!thread_safe)
    require(callback_count.load() == 8,
            "reader suppressed/replaced the caller's HDF5 error handler");
  require(H5Fget_obj_count(H5F_OBJ_ALL, H5F_OBJ_ALL) == handles_before,
          "parallel error path leaked HDF5 handles");
  for (std::size_t i = 0; i < paths.size(); ++i)
    require(std::filesystem::last_write_time(paths[i]) == times[i],
            "parallel reader modified source file");
  // Successful reads after failed opens/parses prove the lock was released and
  // no failure state escaped into subsequent callers.
  for (std::size_t i = 0; i < paths.size(); ++i)
    same_archive(read_archive(paths[i]), baseline[i]);
  std::cout
      << "PASS parallel: threads=" << jobs.size() << " validArchiveReads=" << paths.size() * 9
      << " malformedArchiveReads=16 independentParses=128 exactTypedMetadata=unchanged resourcePayloadBytes=0 errorHandler=unchanged"
      << " hdf5ThreadSafe=" << bool(thread_safe)
      << " observedErrorCallbacks=" << callback_count.load() << " handles=unchanged\n";
}
void fixtures(const std::filesystem::path &directory)
{
  struct Expected {
    const char *name, *label;
    unsigned version, nodes, groups, layers, refs, streams;
  };
  const Expected expected[] = {
      {"dirty-plastic-robot.spsm", "Dirty Plastic Robot", 15, 4871, 1, 4, 24, 3},
      {"shd-hard-rust.spsm", "SHD Hard Rust", 20, 1366, 1, 3, 8, 1},
      {"sratched-metal-pealing-paint-dirt.spsm",
       "Sratched metal, pealing paint, dirt",
       45,
       16157,
       1,
       6,
       104,
       1},
      {"used_varnish_wood.spsm", "Used_Varnish_Wood", 45, 7946, 5, 21, 56, 1},
      {"worn leather chest.spsm", "Worn Leather Chest", 22, 7625, 1, 9, 47, 1}};
  for (const auto &e : expected) {
    const auto path = directory / e.name;
    const auto before = std::filesystem::last_write_time(path);
    const auto archive = read_archive(path);
    require(archive.presets.size() == e.streams, "fixture preset stream count mismatch");
    const auto it = std::find_if(archive.presets.begin(),
                                 archive.presets.end(),
                                 [](const Preset &p) { return p.dataset == "/preset.bin"; });
    require(it != archive.presets.end(), "fixture primary stream missing");
    const auto &preset = *it;
    require(preset.wire_version == e.version && preset.nodes.size() == e.nodes,
            "fixture typed node count/header mismatch");
    const auto *group_node = preset.field(preset.root, "layerGroup");
    require(group_node, "fixture layerGroup missing");
    const auto group_id = static_cast<NodeId>(group_node - preset.nodes.data());
    const auto *label = preset.field(group_id, "label");
    require(label && std::get<std::string>(label->value) == e.label, "fixture label mismatch");
    std::map<std::string, unsigned> classes;
    for (const auto &node : preset.nodes)
      if (const auto *o = std::get_if<Object>(&node.value))
        ++classes[o->class_name];
    require(classes["DataLayerGroup"] == e.groups && classes["DataLayerColor"] == e.layers,
            "fixture hierarchy mismatch");
    const auto primary_index = static_cast<std::size_t>(it - archive.presets.begin());
    unsigned refs = 0, resolved = 0;
    for (const auto &ref : archive.references) {
      if (ref.preset == primary_index)
        ++refs;
      if (ref.uri.find("?version=") != std::string::npos)
        require(!ref.resource_dataset.empty(), "embedded version reference did not resolve");
      resolved += !ref.resource_dataset.empty();
    }
    require(refs == e.refs, "fixture resource reference count mismatch");
    std::uint64_t metadata = 0;
    for (const auto &p : archive.presets)
      metadata += p.encoded.size();
    require(archive.payload_bytes_read == metadata, "reader hydrated a resource payload");
    require(before == std::filesystem::last_write_time(path), "reader modified user fixture");
    std::cout << "PASS " << e.name << ": streams=" << archive.presets.size()
              << " primaryNodes=" << preset.nodes.size() << " groups=" << e.groups
              << " layers=" << e.layers << " refs=" << refs << " resolvedAllStreams=" << resolved
              << " metadataBytes=" << metadata << " resourcePayloadBytes=0\n";
  }
}
}  // namespace
int main(int argc, char **argv)
{
  try {
    H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);  // Malformed fixture tests are expected.
    if (argc == 1) {
      parser_tests();
      hdf_tests();
    }
    else if (argc == 2 && std::string_view(argv[1]) == "--parallel")
      parallel_tests();
    else if (argc == 2)
      fixtures(argv[1]);
    else if (argc == 3 && std::string_view(argv[1]) == "--parallel")
      parallel_tests(argv[2]);
    else
      throw std::runtime_error(
          "usage: bts-spsm-tests [five-fixture-directory | --parallel [five-fixture-directory]]");
    return 0;
  }
  catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
