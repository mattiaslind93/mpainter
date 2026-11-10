// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include "../host/SpsmSource.hh"
#include "SpsmAssetDispatch.hh"
#include "IMB_imbuf.hh"
#include "CLG_log.h"
#include <future>
#include <iostream>
#if defined(__unix__) || defined(__APPLE__)
#  include <sys/stat.h>
#endif
using namespace bts;
namespace {
void require(bool yes, const char *message)
{
  if (!yes)
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
  throw std::runtime_error("Unsafe preparation accepted");
}
StudioAsset asset(const QString &path)
{
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), "fixture open failed");
  StudioAsset a;
  a.filename = QFileInfo(path).fileName();
  a.name = QFileInfo(path).completeBaseName();
  a.path = path;
  a.usages = {"smartmaterial"};
  a.bytes = file.readAll();
  require(a.bytes.size() == file.size(), "fixture read truncated");
  return a;
}
StudioAsset worker(StudioAsset a)
{
  require(hydrate_spsm_asset(a), "worker unexpectedly deferred preparation");
  return a;
}
QString file_hash(const QString &path)
{
  QFile f(path);
  require(f.open(QIODevice::ReadOnly), "source cache open failed");
  QCryptographicHash h(QCryptographicHash::Sha256);
  require(h.addData(&f), "source cache checksum failed");
  return QString::fromLatin1(h.result().toHex());
}
void run(const QString &fixtures)
{
  const auto unknown_color = parse_spsm_source_policy(
      {"spsm:policy:bitmap-color=unknown",
       "spsm:policy:bitmap-uv=projection0-centered-repeat-factors"});
  require(unknown_color.bitmap_color == SpsmBitmapColorInput::Unknown &&
              unknown_color.bitmaps.uv == SpsmBitmapUV::Projection0CenteredRepeatFactors &&
              unknown_color.roughness_raw_r_ignore_a,
          "Unknown Color implied channel use");
  for (auto value :
       {"native-linear-rec709-rgb-levels-ignore-a", "native-acescg-rgb-levels-ignore-a"})
  {
    const auto color = parse_spsm_source_policy(
        {QString("spsm:policy:bitmap-color=") + value,
         "spsm:policy:bitmap-uv=projection0-centered-repeat-factors"});
    require(color.bitmap_color != SpsmBitmapColorInput::Unknown &&
                color.roughness_raw_r_ignore_a &&
                color.bitmaps.uv == SpsmBitmapUV::Projection0CenteredRepeatFactors,
            "Color-only lost common UV or enabled Roughness");
  }
  reject([&] { parse_spsm_source_policy({"spsm:policy:bitmap-color=sRGB"}); }, "Unsupported");
  reject(
      [&] {
        parse_spsm_source_policy({"spsm:policy:bitmap-color=unknown",
                                  "spsm:policy:bitmap-color=native-acescg-rgb-levels-ignore-a"});
      },
      "Conflicting");
  QTemporaryDir cache;
  require(cache.isValid(), "private source cache failed");
  qputenv("BTS_RESOURCE_CACHE", cache.path().toUtf8());
  configure_spsm_source_hydration();
  std::vector<StudioAsset> retained;
  for (const auto name : {"dirty-plastic-robot.spsm",
                          "shd-hard-rust.spsm",
                          "sratched-metal-pealing-paint-dirt.spsm",
                          "used_varnish_wood.spsm",
                          "worn leather chest.spsm"})
  {
    const auto path = QDir(fixtures).filePath(QString::fromUtf8(name));
    if (!QFileInfo::exists(path))
      continue;
    const auto size = QFileInfo(path).size(),
               modified = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    auto a = asset(path);
    const auto original = a.bytes;
    require(!hydrate_spsm_asset(a) && !a.material_preset && spsm_source_report(a).isEmpty() &&
                spsm_source_partial(a),
            "GUI cold miss hydrated images or reported ready");
    const auto classified = material_shelf(a);
    require(classified != MaterialShelf::None, "Cold material is absent from both shelves");
    {
      TextureRamQueryScope no_ram(+[]() -> std::uint64_t { return 0; });
      require(!hydrate_spsm_asset(a) && material_shelf(a) == classified,
              "Cached shelf classification decoded source again");
    }
    auto initial_override = a;
    initial_override.search_terms = {"spsm:policy:uniform-input=ACEScg"};
    require(!hydrate_spsm_asset(initial_override), "GUI cold policy miss hydrated images");
    auto third = std::async(std::launch::async, worker, initial_override);
    auto one = std::async(std::launch::async, worker, a),
         two = std::async(std::launch::async, worker, a);
    auto ready = one.get(), duplicate = two.get();
    const auto source = prepared_spsm_source(ready);
    require(source && source == prepared_spsm_source(duplicate) &&
                ready.material_preset.get() == duplicate.material_preset.get(),
            "worker duplicates did not coalesce preparation");
    require(source->original_bytes->constData() == ready.bytes.constData() &&
                ready.bytes.constData() == duplicate.bytes.constData() &&
                *source->original_bytes == original,
            "original bytes changed or copied across bindings");
    require(ready.bytes == original && ready.filename.endsWith(".spsm") && ready.material_preset,
            "native alias replaced original source with converted preset");
    require(material_shelf(ready) == classified,
            "Metadata-only classification disagrees with fully hydrated content");
    require(source->uniform_input_space == "Linear Rec.709" && source->prepared.partial &&
                !source->prepared.appearance_verified && spsm_source_partial(ready),
            "default inferred uniform policy/partial boundary lost");
    require(source->report.contains("assumptions:") && source->report.contains("/preset.bin") &&
                source->report.contains("-> ACEScg"),
            "precomputed source path report missing");
    require(file_hash(source->cache_path) == source->source_sha256 &&
                QFileInfo(source->cache_path).fileName() == source->source_sha256 + ".spsm",
            "deterministic cache path/hash mismatch");
    {
      TextureRamQueryScope no_ram(+[]() -> std::uint64_t { return 0; });
      require(hydrate_spsm_asset(ready) && spsm_source_report(ready) == source->report,
              "GUI ready lookup queried worker memory/IO or failed to reuse alias");
    }
    auto policy = ready;
    policy.search_terms = {"discoverable", "spsm:policy:uniform-input=ACEScg"};
    auto different = third.get();
    require(hydrate_spsm_asset(policy) &&
                prepared_spsm_source(policy) == prepared_spsm_source(different),
            "GUI ready override failed to reuse concurrent policy preparation");
    const auto other = prepared_spsm_source(different);
    require(other && other != source && other->uniform_input_space == "ACEScg" &&
                other->original_bytes == source->original_bytes &&
                other->cache_path == source->cache_path,
            "policy variant did not share verified original or change preparation");
    require(other->report.contains("Uniform color policy: ACEScg -> ACEScg"),
            "override report claimed default input color space");
    if (QString::fromUtf8(name) == "dirty-plastic-robot.spsm") {
      QFile changed(source->cache_path);
      require(changed.open(QIODevice::ReadWrite), "tamper fixture open failed");
#if defined(__unix__) || defined(__APPLE__)
      struct stat before_stat{};
      require(::fstat(changed.handle(), &before_stat) == 0, "native cache stat failed");
#endif
      require(changed.seek(changed.size() - 1), "tamper seek failed");
      const auto byte = changed.read(1);
      require(byte.size() == 1 && changed.seek(changed.size() - 1), "tamper byte read failed");
      const char altered = char(byte[0] ^ 1);
      require(changed.write(&altered, 1) == 1 && changed.flush(), "tamper write failed");
#if defined(__unix__) || defined(__APPLE__)
#  if defined(__APPLE__)
      const timespec original_times[2] = {before_stat.st_atimespec, before_stat.st_mtimespec};
#  else
      const timespec original_times[2] = {before_stat.st_atim, before_stat.st_mtim};
#  endif
      require(::futimens(changed.handle(), original_times) == 0,
              "restore exact native mtime failed");
#endif
      changed.close();
      auto tamper_policy = ready;
      tamper_policy.search_terms = {"spsm:policy:uniform-input=sRGB"};
      auto repaired = std::async(std::launch::async, worker, tamper_policy).get();
      require(file_hash(prepared_spsm_source(repaired)->cache_path) == source->source_sha256,
              "changed cache with restored mtime bypassed source validation");
    }
    auto bad = ready;
    bad.search_terms = {"spsm:policy:uniform-input=Non-Color"};
    reject([&] { std::async(std::launch::async, worker, bad).get(); }, "data-only");
    bad.search_terms = {"spsm:policy:uniform-input=ACEScg", "spsm:policy:uniform-input=sRGB"};
    reject([&] { std::async(std::launch::async, worker, bad).get(); }, "Conflicting");
    auto reordered = different;
    reordered.search_terms.prepend("other-discovery-tag");
    require(hydrate_spsm_asset(reordered) && prepared_spsm_source(reordered) == other,
            "nonpolicy tags invalidated preparation");
    retained.push_back(ready);
    retained.push_back(different);
    require(QFileInfo(path).size() == size &&
                QFileInfo(path).lastModified().toMSecsSinceEpoch() == modified,
            "artist original fixture changed");
    std::cout << "SOURCE " << name << " sha256=" << source->source_sha256.toStdString()
              << " layers=" << source->prepared.material.set.layers.size()
              << " policies=2 original_bytes=" << ready.bytes.size()
              << " duplicate_shared=1 GUI_deferred=1 appearance_verified=0\n";
  }
  // A catalog reload can own a separate allocation with the same verified SHA.
  auto catalog = retained.front();
  catalog.material_preset.reset();
  catalog.bytes = QByteArray(catalog.bytes.constData(), catalog.bytes.size());
  const auto incoming = catalog.bytes.constData();
  auto normalized = std::async(std::launch::async, worker, catalog).get();
  require(prepared_spsm_source(normalized) == prepared_spsm_source(retained.front()),
          "cached worker did not reuse verified SHA");
  require(catalog.bytes.constData() == incoming &&
              catalog.bytes.constData() != normalized.bytes.constData(),
          "reload fixture did not retain independent catalog buffer");
  {
    TextureRamQueryScope no_ram(+[]() -> std::uint64_t { return 0; });
    require(hydrate_spsm_asset(catalog) &&
                catalog.bytes.constData() == normalized.bytes.constData() &&
                catalog.hash() == normalized.hash(),
            "cached worker failed to register independent catalog buffer for GUI");
  }
  // Corrupt sources fail visibly on workers and leave no pending
  // poison entry: retry still reports the original cause rather than deadlock.
  StudioAsset corrupt;
  corrupt.filename = "corrupt.spsm";
  corrupt.name = "Corrupt";
  corrupt.usages = {"smartmaterial"};
  corrupt.bytes = "not an HDF5 source";
  for (int i = 0; i < 2; ++i)
    reject([&] { std::async(std::launch::async, worker, corrupt).get(); }, "HDF5");
  {
    TextureRamQueryScope low(+[]() -> std::uint64_t { return 32; });
    auto accepted = std::async(std::launch::async, worker, retained.front()).get();
    require(hydrate_spsm_asset(accepted), "RAM telemetry blocked an existing valid source");
  }
  auto unknown = retained.front();
  unknown.search_terms = {"spsm:policy:unverified-procedural-fallback=yes"};
  reject([&] { std::async(std::launch::async, worker, unknown).get(); }, "Unsupported");
  auto nul = retained.front();
  nul.search_terms = {QString("spsm:policy:uniform-input=ACEScg") + QChar::Null + "ignored"};
  reject([&] { std::async(std::launch::async, worker, nul).get(); }, "NUL");
  auto empty = retained.front();
  empty.search_terms = {"spsm:policy:uniform-input="};
  reject([&] { std::async(std::launch::async, worker, empty).get(); }, "Invalid");
  std::weak_ptr<const PreparedSpsmSource> weak = prepared_spsm_source(retained.back());
  std::weak_ptr<const QByteArray> original_owner =
      prepared_spsm_source(retained.back())->original_bytes;
  retained.clear();
  catalog = StudioAsset{};
  normalized = StudioAsset{};
  unknown = StudioAsset{};
  nul = StudioAsset{};
  empty = StudioAsset{};
  require(weak.expired() && original_owner.expired(),
          "weak source/original cache retained completed owners");
  std::cout
      << "PASS source backend: all5 original sources, GUI cold defer/ready noIO, duplicate workers, immutable native aliases, SHA atomic paths, shared originals across policies, explicit OCIO, full reports, corrupt/retry/lowRAM; no GPU or package execution\n";
}
}  // namespace
int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  std::cout << std::unitbuf;
  try {
    require(argc == 2, "Expected five-fixture directory");
    CLG_init();
    blender::IMB_init();
    run(QString::fromUtf8(argv[1]));
    blender::IMB_exit();
    CLG_exit();
    return 0;
  }
  catch (const std::exception &e) {
    std::cerr << "SPSM source backend failed: " << e.what() << '\n';
    return 1;
  }
}
