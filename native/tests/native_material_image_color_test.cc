// SPDX-License-Identifier: GPL-3.0-or-later
// Independent resource-pool regression. This executable creates no GPU context.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "NativeMaterialDecodedImage.hh"
#include <QScopeGuard>
#include "NativeMaterialImageResource.hh"
#include "ResourceAvailability.hh"
#include "BKE_idtype.hh"
#include "BKE_image.hh"
#include "BKE_main.hh"
#include "BKE_report.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DNA_genfile.h"
#include "DNA_image_types.h"
#include "DNA_packedFile_types.h"
#include "IMB_colormanagement.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "CLG_log.h"
#include "MEM_guardedalloc.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <climits>
#include <future>
#include <iostream>
#include <set>

namespace {
using namespace bts;
using namespace blender;
using Pixel = std::array<float, 4>;
using MainOwner = std::unique_ptr<Main, decltype(&BKE_main_free)>;
void require(bool b, const char *s)
{
  if (!b)
    throw std::runtime_error(s);
}
template<class F> void rejects(F f)
{
  bool threw = false;
  try {
    f();
  }
  catch (const std::exception &) {
    threw = true;
  }
  require(threw, "Expected resource rejection");
}
QByteArray sha(const QString &path)
{
  QFile f(path);
  require(f.open(QIODevice::ReadOnly), "Cannot hash own fixture");
  QCryptographicHash h(QCryptographicHash::Sha256);
  require(h.addData(&f), "Fixture hash failed");
  return h.result().toHex();
}
const Pixel literal{.875f, .0625f, .375f, 1};
void exr(const QString &path, Pixel value)
{
  auto *b = IMB_allocImBuf(3, 2, ImBufFlags::FloatData);
  require(b, "No fixture image");
  const auto release = qScopeGuard([&] { IMB_freeImBuf(b); });
  b->channels = 4;
  b->color_mode = ImColorMode::RGBA;
  b->ftype = IMB_FTYPE_OPENEXR;
  b->flags |= ImBufFlags::AlphaChannelPacked;
  for (int i = 0; i < 6; ++i)
    std::copy(value.begin(), value.end(), b->float_data_for_write() + i * 4);
  require(IMB_save_image(b, path.toUtf8().constData(), ImBufFlags::FloatData),
          "Cannot save own EXR");
}
struct Decoded {
  MainOwner main{nullptr, BKE_main_free};
  Image *image = nullptr;
  std::shared_ptr<NativeMaterialImageResource> resource;
  bool reused = false;
};
Decoded decode(const QString &path,
               const QString &source,
               const QString &target,
               eImageAlphaMode alpha = IMA_ALPHA_STRAIGHT,
               bool packed = false,
               bool preload = false,
               bool offline = false)
{
  Decoded d;
  d.main.reset(BKE_main_new());
  IMB_colormanagement_working_space_init_default(d.main.get());
  float3x3 target_to_default;
  for (int column = 0; column < 3; ++column) {
    float basis[4]{};
    basis[column] = basis[3] = 1;
    IMB_colormanagement_transform_v4(
        basis, target.toUtf8().constData(), d.main->colorspace.scene_linear_name);
    for (int row = 0; row < 3; ++row)
      target_to_default[column][row] = basis[row];
  }
  d.main->colorspace.scene_linear_to_xyz = d.main->colorspace.scene_linear_to_xyz *
                                           target_to_default;
  const auto space = target.toUtf8();
  require(space.size() < sizeof(d.main->colorspace.scene_linear_name),
          "Test target name too long");
  STRNCPY(d.main->colorspace.scene_linear_name, space.constData());
  d.image = BKE_image_load(d.main.get(), path.toUtf8().constData());
  require(d.image, "Cannot create fixture Image");
  STRNCPY(d.image->colorspace_settings.name, source.toUtf8().constData());
  d.image->alpha_mode = alpha;
  if (packed) {
    ReportList reports{};
    BKE_reports_init(&reports, RPT_STORE);
    const auto clear = qScopeGuard([&] { BKE_reports_free(&reports); });
    BKE_image_packfiles(&reports, d.image, d.main->filepath);
    require(!d.image->packedfiles.is_empty(), "Cannot pack own fixture");
  }
  require(!offline || packed, "Offline test requires original packed bytes");
  if (offline)
    require(QFile::rename(path, path + ".packed-offline"),
            "Cannot hide own packed fixture source");
  const auto restore_path = qScopeGuard([&] {
    if (offline)
      QFile::rename(path + ".packed-offline", path);
  });
  const auto ram_query = texture_ram_query;
  d.resource = std::async(std::launch::async, [&] {
                 TextureRamQueryScope query(ram_query);
                 std::atomic_bool cancel = false;
                 if (preload) {
                   auto *old = BKE_image_acquire_ibuf(d.image, nullptr, nullptr);
                   require(old, "Cannot seed old global-role BKE cache");
                   BKE_image_release_ibuf(d.image, old, nullptr);
                 }
                 return prepare_shared_native_image(*d.main, *d.image, cancel, d.reused);
               }).get();
  return d;
}
Pixel expected(const Pixel &raw, const char *source, const char *target)
{
  auto p = raw;
  IMB_colormanagement_transform_v4(p.data(), source, target);
  return p;
}
Pixel actual(const Decoded &d)
{
  const auto *b = d.resource->buffer();
  require(b && b->float_data() && b->channels == 4,
          "Normal image pool did not produce explicit graph-space RGBA float pixels");
  Pixel p;
  std::copy_n(b->float_data(), 4, p.begin());
  return p;
}
QJsonObject run(const QString &out, bool baseline)
{
  QDir().mkpath(out);
  const auto file = out + "/recorded-linear.exr", bytefile = out + "/source-srgb.png";
  exr(file, literal);
  QImage png(3, 2, QImage::Format_RGBA8888);
  png.fill(QColor(224, 32, 96));
  require(png.save(bytefile), "Cannot save own PNG");
  const auto float_hash = sha(file), byte_hash = sha(bytefile);
  const QString rec = QString::fromUtf8(IMB_colormanagement_working_space_get_default());
  require(rec != u"ACEScg", "Fixture requires configured default and AP1 to differ");
  const auto prior = std::string(IMB_colormanagement_working_space_get());
  const auto restore = qScopeGuard(
      [&] { IMB_colormanagement_working_space_set_from_name(prior.c_str()); });
  QJsonArray cases;
  double maximum = 0, baseline_error = 0;
  std::vector<Decoded> retained;
  const auto before = native_material_image_pool_statistics().decodes;
  for (const auto &pair :
       std::array<std::pair<QString, QString>, 4>{
           {{rec, "ACEScg"}, {"ACEScg", "ACEScg"}, {"ACEScg", rec}, {rec, rec}}})
  {
    IMB_colormanagement_working_space_set_from_name(pair.first.toUtf8().constData());
    require(QString::fromUtf8(IMB_colormanagement_working_space_get()) == pair.first,
            "Cannot select private test owner working space");
    const auto role = std::string(IMB_colormanagement_working_space_get());
    auto d = decode(file, rec, pair.second);
    const auto want = expected(
        literal, rec.toUtf8().constData(), pair.second.toUtf8().constData());
    const auto got = actual(d);
    double error = 0;
    for (int c = 0; c < 4; ++c)
      error = std::max(error, std::abs(double(got[c]) - want[c]));
    maximum = std::max(maximum, error);
    if (cases.isEmpty())
      baseline_error = error;
    require(std::string(IMB_colormanagement_working_space_get()) == role,
            "Worker changed global working space");
    if (!baseline)
      require(error < 3e-5,
              "Normal pool pixels follow global working space instead of graph-recorded target");
    cases.append(QJsonObject{
        {"owner", pair.first},
        {"graph_recorded_target", pair.second},
        {"error", error},
        {"reused", d.reused},
        {"has_explicit_float_space", d.resource->buffer()->float_buffer.colorspace != nullptr},
        {"reported_float_space",
         QString::fromUtf8(IMB_colormanagement_get_float_colorspace(d.resource->buffer()))}});
    retained.push_back(std::move(d));
  }
  if (baseline) {
    require(baseline_error > 1e-3, "Old normal mismatch was not reproduced");
  }
  else {
    require(retained[0].resource == retained[1].resource &&
                retained[2].resource == retained[3].resource,
            "Global project switch changed explicit target cache identity");
    require(retained[0].resource != retained[2].resource,
            "Recorded legacy and AP1 targets share normalized pixels");
    const auto identity = QJsonDocument::fromJson(retained[0].resource->key().toUtf8()).array();
    require(identity[0].toString() == u"explicit-native-image" &&
                identity[1].toInt() == native_material_decode_policy_version,
            "Normalized resource shares old global-role identity namespace");
    require(native_material_image_pool_statistics().decodes == before + 2,
            "Target-aware source was decoded more than once");
    auto packed = decode(file, rec, "ACEScg", IMA_ALPHA_STRAIGHT, true);
    require(packed.resource == retained[0].resource,
            "Same original packed and file bytes did not share decoded target");
    auto preloaded = decode(file, rec, "ACEScg", IMA_ALPHA_STRAIGHT, false, true);
    require(preloaded.resource == retained[0].resource && actual(preloaded) == actual(retained[0]),
            "Old global-role Image cache contaminated explicit normalized source");
    auto *part = static_cast<ImagePackedFile *>(packed.image->packedfiles.first)->packedfile;
    const int encoded_size = part->size;
    require(MEM_allocN_len(part->data) < INT_MAX,
            "Fixture packed span exceeds native signed field");
    const auto restore_size = qScopeGuard([&] { part->size = encoded_size; });
    part->size = int(MEM_allocN_len(part->data) + 1);
    std::async(std::launch::async, [&] {
      std::atomic_bool cancel = false;
      bool reused = false;
      rejects([&] { prepare_shared_native_image(*packed.main, *packed.image, cancel, reused); });
    }).get();
    part->size = encoded_size;
    std::vector<std::future<Decoded>> concurrent;
    for (int i = 0; i < 3; ++i)
      concurrent.push_back(
          std::async(std::launch::async, [&] { return decode(file, rec, "ACEScg"); }));
    for (auto &job : concurrent)
      require(job.get().resource == retained[0].resource,
              "Concurrent cache reuse duplicated normalized pixels");
    const auto packed_file = out + "/cold-packed.exr";
    const Pixel packed_literal{.375f, .125f, .75f, 1};
    exr(packed_file, packed_literal);
    const auto packed_sha = sha(packed_file);
    const auto before_packed = native_material_image_pool_statistics().decodes;
    auto cold_packed = decode(packed_file, rec, "ACEScg", IMA_ALPHA_STRAIGHT, true, false, true);
    require(native_material_image_pool_statistics().decodes == before_packed + 1,
            "Cold packed fixture reused a previous decoded resource");
    require(actual(cold_packed) == expected(packed_literal, rec.toUtf8().constData(), "ACEScg"),
            "Offline original PackedFile bytes did not normalize into graph target");
    require(sha(packed_file) == packed_sha,
            "Offline packed decode changed original encoded source");
    require(decode(packed_file, rec, "ACEScg").resource == cold_packed.resource,
            "File source did not reuse cold packed normalization");
    const auto cold_file = out + "/parallel-source.exr";
    exr(cold_file, {.625f, .5f, .25f, 1});
    const auto before_parallel = native_material_image_pool_statistics().decodes;
    concurrent.clear();
    for (int i = 0; i < 3; ++i)
      concurrent.push_back(
          std::async(std::launch::async, [&] { return decode(cold_file, rec, "ACEScg"); }));
    std::vector<Decoded> parallel;
    for (auto &job : concurrent)
      parallel.push_back(job.get());
    require(native_material_image_pool_statistics().decodes == before_parallel + 1,
            "Concurrent cold source duplicated worker normalization");
    for (const auto &d : parallel)
      require(d.resource == parallel.front().resource,
              "Concurrent producer did not publish one immutable buffer");
    {
      std::atomic_bool cancel = true;
      bool reused = false;
      rejects([&] {
        prepare_shared_native_image(
            *parallel.front().main, *parallel.front().image, cancel, reused);
      });
    }
    require(decode(cold_file, rec, "ACEScg").resource == parallel.front().resource,
            "Cancelled request corrupted ready normalized resource");
    std::set<const NativeMaterialImageResource *> alpha_resources;
    std::vector<Decoded> alpha_retained;
    for (auto alpha :
         {IMA_ALPHA_STRAIGHT, IMA_ALPHA_PREMUL, IMA_ALPHA_IGNORE, IMA_ALPHA_CHANNEL_PACKED})
    {
      auto d = decode(file, rec, "ACEScg", alpha);
      alpha_resources.insert(d.resource.get());
      alpha_retained.push_back(std::move(d));
    }
    require(alpha_resources.size() == 4, "Different source alpha policies share a cache key");
    auto data = decode(file, "Non-Color", "ACEScg", IMA_ALPHA_CHANNEL_PACKED);
    require(actual(data) == literal, "Data pixels were color transformed");
    const auto mutable_file = out + "/same-size-source.exr";
    exr(mutable_file, literal);
    const QFileInfo stamp(mutable_file);
    auto initial = decode(mutable_file, rec, "ACEScg");
    const Pixel changed{.125f, .875f, .25f, 1};
    exr(mutable_file, changed);
    require(QFileInfo(mutable_file).size() == stamp.size(),
            "Same-size source fixture setup changed encoded length");
    {
      QFile f(mutable_file);
      require(f.open(QIODevice::ReadWrite) &&
                  f.setFileTime(stamp.lastModified(), QFileDevice::FileModificationTime),
              "Cannot restore fixture mtime");
    }
    auto updated = decode(mutable_file, rec, "ACEScg");
    require(updated.resource != initial.resource &&
                actual(updated) == expected(changed, rec.toUtf8().constData(), "ACEScg"),
            "Same-size source rewrite with restored mtime reused stale normalized cache");
    const Pixel srgb{224 / 255.f, 32 / 255.f, 96 / 255.f, 1};
    auto bytes = decode(bytefile, "sRGB", "ACEScg");
    const auto want = expected(srgb, "sRGB", "ACEScg"), got = actual(bytes);
    for (int c = 0; c < 4; ++c)
      require(std::abs(got[c] - want[c]) < 3e-5, "Byte normalized cache is not graph-target RGB");
    const auto failure = std::async(std::launch::async, [&] {
                           rejects([&] { decode(out + "/missing.exr", rec, "ACEScg"); });
                         }).share();
    failure.get();
    require(decode(file, rec, "ACEScg").resource == retained[0].resource,
            "Failed independent load corrupted shared ready resource");
    const auto low = out + "/low-budget.exr";
    exr(low, {.25f, .75f, .125f, 1});
    {
      TextureRamQueryScope scope([]() -> std::uint64_t { return 0; });
      ([&] { decode(low, rec, "ACEScg"); })();
    }
    require(actual(decode(low, rec, "ACEScg")) ==
                expected({.25f, .75f, .125f, 1}, rec.toUtf8().constData(), "ACEScg"),
            "Failed low-RAM producer poisoned subsequent decode");
  }
  require(sha(file) == float_hash && sha(bytefile) == byte_hash,
          "Original encoded source bytes changed");
  return {{"cpu_only", true},
          {"baseline", baseline},
          {"cases", cases},
          {"maximum_error", maximum},
          {"baseline_first_error", baseline_error},
          {"source_exr_sha256", QString::fromLatin1(float_hash)},
          {"source_png_sha256", QString::fromLatin1(byte_hash)},
          {"original_source_unchanged", true},
          {"same_size_restored_mtime_invalidates", !baseline},
          {"packed_file_shared", !baseline},
          {"cold_packed_source_unavailable", !baseline},
          {"alpha_policy_isolated", !baseline},
          {"failed_producer_retry", !baseline},
          {"concurrent_cold_single_decode", !baseline},
          {"cancel_preserves_ready_cache", !baseline},
          {"preloaded_global_role_cache_replaced", !baseline}};
}
}  // namespace
int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  std::cout << std::unitbuf;
  int result = 1;
  CLG_init();
  blender::DNA_sdna_current_init();
  blender::BKE_idtype_init();
  blender::IMB_init();
  try {
    const auto args = app.arguments();
    require(args.size() == 2 || args.size() == 3,
            "Expected private output directory [--baseline|--runtime-baseline]");
    QDir().mkpath(args[1]);
    const auto report = args.size() == 3 && args[2] == u"--runtime-baseline" ?
                            QJsonObject{{"runtime_baseline", true}} :
                            run(args[1], args.size() == 3 && args[2] == u"--baseline");
    QFile f(args[1] + "/cpu.json");
    require(f.open(QIODevice::WriteOnly), "Cannot write proof");
    f.write(QJsonDocument(report).toJson());
    std::cout << "NATIVE_IMAGE_COLOR_CPU_PASS\n";
    result = 0;
  }
  catch (const std::exception &e) {
    std::cerr << "NATIVE_IMAGE_COLOR_CPU_FAIL " << e.what() << '\n';
  }
  blender::IMB_exit();
  blender::DNA_sdna_current_free();
  CLG_exit();
  return result;
}
