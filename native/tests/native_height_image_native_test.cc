// SPDX-License-Identifier: GPL-3.0-or-later
// Private diagnostic actor; native/GPU execution requires a coherent approved build.
// Reuse the established owner fixture and actual product Save/Open boundaries.
#define BTS_HEIGHT_POLICY_NO_MAIN
#include "native_height_policy_native_test.cc"
#undef BTS_HEIGHT_POLICY_NO_MAIN
#include "NativeHeightImageFixture.hh"
#include "NativeMaterialImages.hh"
#include "NativeMaterialImageDependencies.hh"
#include "IMB_imbuf.hh"
#include "NativeMaterialImageResource.hh"
#include "BLI_math_half.hh"

namespace image_height_owner {
using namespace height_policy_owner;
using namespace bts::height_image_fixture;
struct Source {
  StudioAsset asset;
  std::vector<StudioAsset> textures;
  std::shared_ptr<NativeMaterialGraph> graph;
  QString image, path;
};
template<class Action> void rejects(Action &&action, const char *message)
{
  bool rejected = false;
  try {
    action();
  }
  catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, message);
}
Source source(const QString &directory, bool linear = true, bool repeat = true)
{
  return work([&] {
    QDir().mkpath(directory);
    const auto image = directory + "/height-only.exr",
               path = directory + "/height-source.btsnative";
    write_exr(image);
    std::atomic_bool cancel = false;
    auto owner = finish_native_material_source(
        graph(image, linear, repeat),
        cancel,
        nullptr,
        NativeWorldNormalSurfaceContract::PainterUndisplaced);
    const auto ordinary = prepare_native_material_images(*owner, cancel);
    require(ordinary.isEmpty() && owner->image_resources.empty(),
            "Height-only image leaked into ordinary Color/Metal/Rough preparation");
    auto archive = archive_native_material(*owner, path, cancel, true);
    require(archive.textures.size() == 1,
            "Height-only original image was not archived exactly once");
    auto asset = native_material_asset(
        "Image-backed authored Height", archive.bytes, archive.description);
    for (const auto &channel : owner->channels)
      if (channel.tree)
        asset.search_terms.append("native-channel:" + QString::number(channel.channel));
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(archive.bytes) == archive.bytes.size(),
            "Cannot save own native Height source");
    return Source{std::move(asset), std::move(archive.textures), std::move(owner), image, path};
  });
}
void retain_project_source(Source &source, const QString &library)
{
  AssetCatalog catalog(library);
  for (const auto &texture : source.textures)
    catalog.add(texture);
  catalog.add(source.asset);
  const auto id = catalog.retain_project(source.asset);
  const auto project = catalog.project_assets();
  const auto found = std::find_if(
      project.begin(), project.end(), [&](const auto &asset) { return asset.id == id; });
  require(found != project.end(), "Native source was not retained by the actual catalog route");
  source.asset = *found;
  source.textures.clear();
  for (const auto &asset : project)
    if (asset.usages.contains("texture"))
      source.textures.push_back(asset);
  require(source.asset.scope == AssetScope::Project && source.textures.size() == 1 &&
              source.textures.front().scope == AssetScope::Project,
          "Catalog did not retain the Height-only image cohort");
  validate_native_material_dependencies(source.asset, project);
}
QJsonArray retained_images(const std::vector<StudioAsset> &resources)
{
  QJsonArray records;
  for (const auto &asset : resources)
    if (asset.usages.contains("texture")) {
      require(asset.scope == AssetScope::Project, "Packed image did not retain project scope");
      if (!asset.texture_files.empty())
        for (const auto &file : asset.texture_files)
          require(QFileInfo(file.path).size() == file.size &&
                      stream_hash(file.path) == file.sha256,
                  "Packed image file size or actual encoded checksum changed");
      else
        require(
            !asset.bytes.isEmpty() &&
                QString::fromLatin1(
                    QCryptographicHash::hash(asset.bytes, QCryptographicHash::Sha256).toHex()) ==
                    asset.hash(),
            "Packed encoded image bytes changed");
      records.append(
          QJsonObject{{"id", asset.id.toString(QUuid::WithoutBraces)}, {"sha256", asset.hash()}});
    }
  require(!records.isEmpty(), "Packed image witness is empty");
  return records;
}
QJsonObject cpu(const QString &directory)
{
  require(!GPU_context_active_get(), "Image Height CPU admission created a GPU context");
  const auto compiles = GPU_pass_global_compilation_count();
  auto result = work([&] {
    QDir().mkpath(directory);
    const auto image = directory + "/cpu-height.exr";
    write_exr(image);
    const auto image_sha = stream_hash(image);
    QJsonArray cases;
    std::atomic_bool cancel = false;
    for (const auto mutation : {Mutation::None,
                                Mutation::Alpha,
                                Mutation::ImplicitColor,
                                Mutation::ColorSpace,
                                Mutation::Generated,
                                Mutation::Udims,
                                Mutation::CustomNormal,
                                Mutation::HSV,
                                Mutation::Green,
                                Mutation::MathAfterRed,
                                Mutation::Cubic,
                                Mutation::Clip,
                                Mutation::Environment})
    {
      bool accepted = false;
      try {
        auto prepared = prepare_native_raw_height(graph(image, true, true, mutation), cancel);
        const auto requirements = native_height_requirements(prepared);
        validate_native_height_requirements(requirements);
        accepted = true;
        require(requirements.images && requirements.scalar_image,
                "Image Height admission lacks actual raw image closure");
      }
      catch (const std::invalid_argument &) {
        if (mutation == Mutation::None)
          throw;
      }
      require(accepted == (mutation == Mutation::None),
              "Image Height structural admission widened unexpectedly");
      cases.append(QJsonObject{{"mutation", int(mutation)}, {"accepted", accepted}});
    }
    // Literal source-only layouts: worker data decode must preserve R even at
    // alpha zero. Keep the shared raw owner alive while inspecting its resource.
    QJsonArray layouts;
    for (const auto layout : {Layout::RGB, Layout::Y, Layout::RGBAZero}) {
      const auto file = directory + QString("/layout-%1.exr").arg(int(layout));
      write_exr(file, layout);
      for (const auto alpha : {IMA_ALPHA_STRAIGHT, IMA_ALPHA_PREMUL, IMA_ALPHA_CHANNEL_PACKED}) {
        auto detached = graph(file);
        for (auto &image : detached.main->images)
          image.alpha_mode = alpha;
        auto raw = prepare_native_raw_height(std::move(detached), cancel);
        validate_native_height_requirements(native_height_requirements(raw));
        require(prepare_native_material_images(*raw.source, cancel).isEmpty(),
                "Height-only layout leaked to ordinary decode");
        std::array<bNodeTree *, 1> roots{raw.tree};
        prepare_native_material_images(*raw.source, cancel, roots, false);
        require(raw.source->image_resources.size() == 1,
                "Raw-only layout did not retain exactly one shared decoded image");
        const auto &[image, resource] = raw.source->image_resources.front();
        const auto *buffer = resource->buffer();
        require(image->alpha_mode == alpha && bool(image->flag & IMA_HIGH_BITDEPTH) &&
                    QString::fromUtf8(image->colorspace_settings.name) == "Non-Color",
                "Raw worker changed immutable image policy");
        require(buffer && buffer->float_data() && buffer->channels == 4 &&
                    buffer->x == height_image_reference::fixture_width &&
                    buffer->y == height_image_reference::fixture_height,
                "Raw worker layout/dimensions changed");
        const auto expected = height_image_reference::fixture_pixels();
        double error = 0;
        for (std::size_t p = 0; p < expected.size(); ++p)
          for (int c = 0; c < 4; ++c) {
            const float wanted = c == 3              ? (layout == Layout::RGBAZero ? 0.f : 1.f) :
                                 layout == Layout::Y ? expected[p][0] :
                                                       expected[p][c];
            error = std::max(error, std::abs(double(buffer->float_data()[p * 4 + c]) - wanted));
          }
        require(
            error < 1e-6,
            "Signed/HDR data layout was color-converted or alpha-clipped during worker decode");
        layouts.append(QJsonObject{{"layout", int(layout)},
                                   {"encoded_channels", exr_channels(file)},
                                   {"alpha_mode", int(alpha)},
                                   {"maximum_literal_error", error}});
      }
    }
    auto owned = source(directory + "/portable");
    std::vector<StudioAsset> resources = owned.textures;
    resources.push_back(owned.asset);
    const auto sha = owned.asset.hash();
    const auto metadata = asset_metadata(owned.asset),
               state = source_state(*owned.graph->material->nodetree);
    auto prepared = prepare_native_material_height_resource(owned.asset, resources, cancel);
    require(owned.asset.hash() == sha && asset_metadata(owned.asset) == metadata &&
                source_state(*owned.graph->material->nodetree) == state,
            "Image-backed worker preparation changed source/default metadata");
    rejects(
        [&] {
          std::atomic_bool stop = true;
          prepare_native_material_height_resource(owned.asset, resources, stop);
        },
        "Cancelled image Height worker was admitted");
    rejects([&] { prepare_native_material_height_resource(owned.asset, {}, cancel); },
            "Missing Height-only packed image was admitted");
    auto changed = resources;
    changed.front().texture_files.clear();
    changed.front().bytes.append('x');
    rejects([&] { prepare_native_material_height_resource(owned.asset, changed, cancel); },
            "Changed byte-backed image SHA was admitted");
    require(!resources.front().texture_files.empty(),
            "Image hash fixture needs its actual disk-backed resource");
    const auto resource_path = resources.front().texture_files.front().path;
    QFile original(resource_path);
    require(original.open(QIODevice::ReadOnly), "Cannot preserve own disk-backed image");
    const auto original_bytes = original.readAll();
    original.close();
    {
      const auto restore = qScopeGuard([&] {
        QSaveFile file(resource_path);
        require(file.open(QIODevice::WriteOnly) &&
                    file.write(original_bytes) == original_bytes.size() && file.commit(),
                "Cannot restore own image after checksum negative");
      });
      QFile corrupt(resource_path);
      require(corrupt.open(QIODevice::Append) && corrupt.write("x", 1) == 1,
              "Cannot alter own disk-backed image");
      corrupt.close();
      rejects([&] { prepare_native_material_height_resource(owned.asset, resources, cancel); },
              "Changed disk-backed image SHA was admitted");
    }
    require(stream_hash(image) == image_sha, "CPU admission changed original EXR bytes");
    return QJsonObject{{"layouts", layouts},
                       {"cases", cases},
                       {"worker_decode", true},
                       {"source_unchanged", true},
                       {"cancel_missing_changed_image_rejected", true},
                       {"ordinary_image_decode_count", 0},
                       {"interactive_cpu_composition", 0}};
  });
  require(!GPU_context_active_get() && GPU_pass_global_compilation_count() == compiles,
          "Image Height CPU oracle compiled or created GPU state");
  return result;
}
QJsonObject verify(Harness &h,
                   const QUuid &id,
                   bool linear,
                   bool repeat,
                   bool partial = false,
                   bool half_layer = false)
{
  const auto &selected = TextureDocument::layer(h.set(), id);
  const auto source_pixels = height_image_reference::fixture_pixels();
  require(selected.fill.mode == 0 && !selected.fill.physical && selected.fill.rotation == 0 &&
              selected.fill.scale == std::array<double, 2>{1, 1},
          "Image oracle assumes only an explicit UV translation");
  double maximum = 0, low = 1e30, high = -1e30;
  std::uint64_t components = 0;
  QJsonArray tile_samples;
  for (int tile : {1001, 1002}) {
    std::vector<std::array<int, 2>> positions;
    for (int y = 0; y < 128; ++y)
      for (int x = 0; x < 128; ++x)
        positions.push_back({x, y});
    const auto actual = h.pixels(positions, tile);
    require(actual.size() == 128 * 128,
            "Image Height oracle did not read the complete destination tile");
    for (std::size_t i = 0; i < positions.size(); ++i) {
      const auto p = positions[i];
      const std::array<double, 2> uv{
          (tile - 1001) % 10 + (p[0] + .5) / 128 - selected.fill.offset[0],
          (tile - 1001) / 10 + (p[1] + .5) / 128 - selected.fill.offset[1]};
      const double raw = height_image_reference::sample(source_pixels,
                                                        height_image_reference::fixture_width,
                                                        height_image_reference::fixture_height,
                                                        uv[0],
                                                        uv[1],
                                                        linear,
                                                        repeat),
                   coverage = partial ? double(.4f) * double(.6f) : 1.;
      low = std::min(low, raw);
      high = std::max(high, raw);
      for (int c = 0; c < 4; ++c) {
        double expected = c == 3 ? coverage : raw * coverage;
        if (half_layer)
          expected = math::half_to_float(math::float_to_half_make_finite(float(expected)));
        const auto error = std::abs(actual[i][c] - expected);
        maximum = std::max(maximum, error);
        require(std::isfinite(actual[i][c]) && error < scalar_bound,
                "Image-backed raw Height differs from independent source-image sampler");
        ++components;
      }
    }
    // Retain a small inspectable packet as well as checking every destination texel.
    tile_samples.append(samples_json(h.pixels({{13, 17}, {37, 53}, {77, 91}, {109, 113}}, tile)));
  }
  require(low < -.5 && high > 1.5, "Image Height oracle has no signed/HDR witness");
  return {
      {"components", qint64(components)},
      {"maximum_error", maximum},
      {"fixed_bound", scalar_bound},
      {"minimum", low},
      {"maximum", high},
      {"linear", linear},
      {"repeat", repeat},
      {"partial", partial},
      {"layer_precision", half_layer ? "L16F" : "L32F"},
      {"graph_precision", "F32"},
      {"samples", tile_samples},
      {"reference",
       "literal F32 EXR R + independent CPU diagnostic sampler; production native image node on GPU"}};
}
void select(Harness &h, const QUuid &id)
{
  for (auto &layer : h.set().layers)
    if (layer.fill.native_material)
      layer.visible = layer.id == id;
  h.set().selected_layer = id;
  h.set().selected_mask = false;
  h.set().selected_effect = {};
  h.publish();
}
QJsonObject cold(Harness &h)
{
  QFile oracle(h.directory + "/image-restart-oracle.json");
  require(oracle.open(QIODevice::ReadOnly), "Image Height restart oracle is missing");
  const auto expected = QJsonDocument::fromJson(oracle.readAll()).object();
  require(!QFileInfo::exists(h.directory + "/source/height-only.exr") &&
              !QFileInfo::exists(h.directory + "/source/height-source.btsnative"),
          "Cold Image Height still has original source paths");
  const auto project = h.directory + "/packed-image-height.blend";
  require(stream_hash(project) == expected["project_sha256"].toString(),
          "Cold Image Height packed source changed");
  h.create();
  auto pending = work([&] { return PendingProject::prepare(project.toStdString()); });
  pending->validate();
  require(packed_contract(*pending->document, pending->resources) ==
              expected["contract"].toObject(),
          "Data-first cold policy/source/UUID contract changed");
  const auto image_data = retained_images(pending->resources);
  require(image_data == expected["retained_images"].toArray(),
          "Data-first packed image bytes/identities changed");
  save_json(h.directory + "/image-cold-data.json",
            {{"before_explicit_gpu_staging", true},
             {"retained_images", image_data},
             {"contract", packed_contract(*pending->document, pending->resources)}});
  auto gpu = h.host.begin_project_gpu(*pending);
  require(bool(gpu), "Cold Image Height skipped isolated GPU staging");
  h.wait([&] { return h.host.step_project_gpu(*pending, *gpu); },
         "Cold image Height staging timed out");
  h.resources = pending->resources;
  h.document = h.host.adopt_project(std::move(pending), project.toStdString(), std::move(gpu));
  require(packed_contract(h.document, h.resources) == expected["contract"].toObject(),
          "Cold Image Height changed policy/source/alias metadata");
  const QUuid first(expected["first"].toString()), second(expected["second"].toString()),
      none(expected["none"].toString());
  const auto asset = std::find_if(h.resources.begin(), h.resources.end(), [&](const auto &entry) {
    return entry.id.toString(QUuid::WithoutBraces).toStdString() ==
           TextureDocument::layer(h.set(), first).fill.material_resource;
  });
  require(asset != h.resources.end(), "Cold raw source is missing");
  h.source_none(*asset, none, first);
  select(h, first);
  const auto a = verify(h, first, true, true);
  select(h, second);
  const auto b = verify(h, second, true, true);
  require(a["samples"] == expected["first_samples"] && b["samples"] == expected["second_samples"],
          "Fresh packed image Height did not preserve exact saved GPU samples");
  QJsonArray samplers;
  for (const auto &entry : expected["samplers"].toArray()) {
    const auto saved = entry.toObject();
    const QUuid id(saved["layer"].toString());
    select(h, id);
    const auto actual = verify(h, id, saved["linear"].toBool(), saved["repeat"].toBool());
    require(actual["samples"] == saved["samples"], "Cold sampler pixels changed");
    samplers.append(actual);
  }
  return {{"samplers", samplers},
          {"fresh_process", true},
          {"source_paths_absent", true},
          {"first", a},
          {"second", b},
          {"contract", packed_contract(h.document, h.resources)}};
}
QJsonObject warm(Harness &h)
{
  h.create();
  auto owned = source(h.directory + "/source");
  retain_project_source(owned, h.directory + "/own-source-library");
  h.resources = owned.textures;
  h.install(owned.asset, owned.graph);
  const auto hash = owned.asset.hash(), image_hash = stream_hash(owned.image);
  const auto metadata = asset_metadata(owned.asset);
  const auto none = h.apply(owned.asset), first = h.apply(owned.asset),
             second = h.apply(owned.asset);
  h.publish();
  // A partially transferred candidate cannot publish a policy, and cancellation
  // leaves the actual document/ordinary source unaffected.
  auto resource = work([&] {
    std::atomic_bool cancel = false;
    return prepare_native_material_height_resource(owned.asset, h.resources, cancel);
  });
  const auto before = h.document.to_json();
  auto work_gpu = h.host.stage_native_height_gpu(owned.asset.id, std::move(resource));
  require(!work_gpu->ready(), "Height candidate skipped its upload boundary");
  rejects([&] { h.host.install_native_height_gpu(*work_gpu); },
          "An unready Height image was published");
  require(!h.host.step_native_height_gpu(*work_gpu),
          "Height image allocation and compilation collapsed into one owner step");
  work_gpu.reset();
  require(h.document.to_json() == before,
          "Cancelled image upload changed authored document state");
  // A separate fresh workspace process must exercise the actual first-enable
  // path, not inherit a Raw sidecar prepared by another saved Raw instance.
  const auto ui_none = h.directory + "/none-image-height-ui.blend";
  h.host.save_project(h.document, ui_none.toStdString(), h.resources);
  save_json(h.directory + "/none-image-height-ui.json",
            {{"project_sha256", stream_hash(ui_none)},
             {"set", h.set().id.toString()},
             {"layer", none.toString()},
             {"source", owned.asset.id.toString()},
             {"source_sha256", hash},
             {"width", 128},
             {"tiles", QJsonArray{1001, 1002}},
             {"linear", true},
             {"repeat", true},
             {"graph_precision", "F32"},
             {"layer_precision", "L32F"}});
  h.activate(owned.asset, first);
  h.activate(owned.asset, second);
  TextureDocument::layer(h.set(), second).fill.offset = {.1875, -.0625};
  select(h, first);
  const auto a = verify(h, first, true, true);
  select(h, second);
  const auto b = verify(h, second, true, true);
  require(a["samples"] != b["samples"],
          "Image Height instances did not retain independent UV transforms");
  const auto unchanged = h.set();
  TextureDocument::layer(h.set(), second).fill.offset = {-.125, .25};
  h.publish();
  const auto edit = verify(h, second, true, true);
  require(edit["samples"] != b["samples"], "Edited UV mapping reused stale image Height pixels");
  h.set() = unchanged;
  h.publish();
  require(verify(h, second, true, true)["samples"] == b["samples"],
          "Restoring image Height mapping did not restore exact GPU values");
  // Same cache/source identity under a preview-only transport hash remains
  // covered by native_material_preview_height_alias_test.cc; immutable image
  // changes and foreign aliases must remain rejected by those existing guards.
  auto &layer = TextureDocument::layer(h.set(), second);
  layer.opacity[4] = .4f;
  layer.mask = LayerMask{};
  layer.mask->background = .6f;
  h.publish();
  const auto partial = verify(h, second, true, true, true);
  h.set() = unchanged;
  h.publish();
  require(owned.asset.hash() == hash && asset_metadata(owned.asset) == metadata &&
              stream_hash(owned.image) == image_hash,
          "Image Height activation/edit changed original source identities");
  QJsonArray sampler_cases;
  QJsonObject half;
  QStringList extra_sources;
  for (bool linear : {false, true})
    for (bool repeat : {false, true}) {
      if (linear && repeat)
        continue;
      auto sampler = source(
          h.directory + QString("/samplers/%1-%2").arg(linear).arg(repeat), linear, repeat);
      retain_project_source(
          sampler, h.directory + QString("/own-sampler-library-%1-%2").arg(linear).arg(repeat));
      for (const auto &texture : sampler.textures)
        if (std::none_of(h.resources.begin(), h.resources.end(), [&](const auto &asset) {
              return asset.id == texture.id;
            }))
          h.resources.push_back(texture);
      h.install(sampler.asset, sampler.graph);
      const auto layer_id = h.apply(sampler.asset);
      h.activate(sampler.asset, layer_id);
      TextureDocument::layer(h.set(), layer_id).fill.offset = {-.1875, .125};
      select(h, layer_id);
      auto sampler_proof = verify(h, layer_id, linear, repeat);
      sampler_proof["layer"] = layer_id.toString();
      sampler_cases.append(sampler_proof);
      if (!linear && repeat) {
        h.set().precision[4] = ChannelPrecision::L16F;
        h.publish();
        half = verify(h, layer_id, linear, repeat, false, true);
        h.set().precision[4] = ChannelPrecision::L32F;
        h.publish();
      }
      extra_sources << sampler.image << sampler.path;
    }
  select(h, second);
  h.source_none(owned.asset, none, first);

  for (const auto &resource : h.resources)
    require(resource.scope == AssetScope::Project,
            "Own packed project cohort contains an unretained Session resource");
  const auto project = h.directory + "/packed-image-height.blend";
  h.host.save_project(h.document, project.toStdString(), h.resources);
  save_json(h.directory + "/image-restart-oracle.json",
            {{"project_sha256", stream_hash(project)},
             {"retained_images", retained_images(h.resources)},
             {"contract", packed_contract(h.document, h.resources)},
             {"none", none.toString()},
             {"first", first.toString()},
             {"second", second.toString()},
             {"first_samples", a["samples"]},
             {"second_samples", b["samples"]},
             {"samplers", sampler_cases}});
  require(QFile::rename(owned.image, owned.image + ".held") &&
              QFile::rename(owned.path, owned.path + ".held"),
          "Cannot hide own Image Height source paths");
  for (const auto &path : extra_sources)
    require(QFile::rename(path, path + ".held"), "Cannot hide extra sampler source");
  owned.graph.reset();
  return {{"samplers", sampler_cases},
          {"first", a},
          {"half_layer", half},
          {"second", b},
          {"edited", edit},
          {"partial", partial},
          {"source_hash", hash},
          {"image_hash", image_hash},
          {"unready_rejected_and_cancelled", true},
          {"two_instances", true},
          {"destination_tiles", QJsonArray{1001, 1002}},
          {"default_source_policy", "none"},
          {"active_bump_response_equivalence", false},
          {"interactive_cpu_composition", 0}};
}
}  // namespace image_height_owner
int main(int argc, char **argv)
{
  using namespace image_height_owner;
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QSurfaceFormat format;
  format.setVersion(4, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setDepthBufferSize(24);
  QSurfaceFormat::setDefaultFormat(format);
  initialize_numeric_locale();
  QApplication app(argc, argv);
  try {
    require(argc >= 2, "Expected private image Height output directory");
    const QString directory = QString::fromLocal8Bit(argv[1]);
    QDir().mkpath(directory);
    if (argc == 3 && QString::fromLocal8Bit(argv[2]) == "--cpu-only") {
      BlenderHost host(QCoreApplication::applicationFilePath().toStdString(), "");
      const auto proof = cpu(directory);
      host.shutdown();
      std::puts("BTS_HEIGHT_IMAGE_CLEAN");
      save_json(directory + "/image-cpu.json", proof);
      std::puts("HEIGHT_IMAGE_CPU PASS");
      return 0;
    }
    Harness h(directory);
    const bool reopened = argc == 3 && QString::fromLocal8Bit(argv[2]) == "--cold-only";
    auto proof = reopened ? cold(h) : warm(h);
    h.shutdown();
    std::puts("BTS_HEIGHT_IMAGE_CLEAN");
    save_json(directory + (reopened ? "/image-cold.json" : "/image-warm.json"), proof);
    std::puts(reopened ? "HEIGHT_IMAGE_COLD PASS" : "HEIGHT_IMAGE_WARM PASS");
    return 0;
  }
  catch (const std::exception &error) {
    std::fprintf(stderr, "HEIGHT_IMAGE_FAILED %s\n", error.what());
    return 1;
  }
}
