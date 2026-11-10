// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Initialized CPU fixture for immutable encoded UDIM ownership and transport.
// Unit A deliberately creates no native uploader, sampler or GPU program.
#include "NativeFragmentRetainedImageContextTest.hh"
#include "NativeMaterialDecodedImage.hh"
#include "NativeMaterialImageSource.hh"
#include "NativeMaterialBundle.hh"
#include "NativeMaterialSourceFiles.hh"
#include "NativeMaterialSourceBundleCompatibilityTest.hh"
#include "opencolorio/OCIO_matrix.hh"
#include "BKE_image.hh"
#include "DNA_packedFile_types.h"
#include <cstring>
#include <QDir>
#if defined(__linux__)
#  include <sys/mman.h>
#  include <unistd.h>
#endif

namespace bts::native_tiled_source_test {
using native_fragment_retained_image_test::require;
using native_fragment_retained_image_test::socket;
using native_fragment_retained_image_test::work;
using native_fragment_retained_image_test::image_node;
inline constexpr std::array<int, 4> numbers{1001, 1002, 1011, 2000};
inline constexpr std::array<std::array<int, 2>, 4> dimensions{
    {{11, 7}, {7, 13}, {17, 5}, {5, 19}}};
inline QString path(const QString &directory, int tile)
{
  return QDir(directory).filePath(QString("source.%1.exr").arg(tile));
}
inline std::array<float, 4> pixel(int member, int x, int y)
{
  return {-.5f + .125f * x + .03125f * member,
          1.25f + .0625f * y,
          .25f + .03125f * (x + y + member),
          (x + y) % 2 ? .5f : 0.f};
}
inline void write(const QString &directory, int changed_tile = 0)
{
  using namespace blender;
  for (std::size_t i = 0; i < numbers.size(); ++i) {
    if (changed_tile && numbers[i] != changed_tile)
      continue;
    const auto [w, h] = dimensions[i];
    auto *buffer = IMB_allocImBuf(w, h, ImBufFlags::FloatData);
    require(buffer, "Cannot allocate tiled fixture pixels");
    const auto release = qScopeGuard([&] { IMB_freeImBuf(buffer); });
    buffer->channels = 4;
    buffer->color_mode = ImColorMode::RGBA;
    buffer->ftype = IMB_FTYPE_OPENEXR;
    buffer->flags |= ImBufFlags::AlphaChannelPacked;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        auto value = pixel(int(i), x, y);
        if (numbers[i] == changed_tile)
          value[0] += 1.f;
        std::copy(value.begin(), value.end(), buffer->float_data_for_write() + 4 * (y * w + x));
      }
    require(IMB_save_image(
                buffer, path(directory, numbers[i]).toUtf8().constData(), ImBufFlags::FloatData),
            "Cannot encode tiled fixture EXR");
  }
}
inline blender::Image &image(NativeMaterialGraph &graph)
{
  return *reinterpret_cast<blender::Image *>(image_node(*graph.material->nodetree).id);
}
inline std::shared_ptr<NativeMaterialGraph> graph(const QString &directory,
                                                  const char *space,
                                                  const std::atomic_bool &cancel,
                                                  bool tiled = true,
                                                  int file_tile = 1001,
                                                  bool ordinary = true,
                                                  bool relative = false)
{
  using namespace blender;
  auto source = native_fragment_retained_image_test::fixture(
      path(directory, file_tile), IMA_ALPHA_CHANNEL_PACKED, space);
  STRNCPY(source.main->colorspace.scene_linear_name, "ACEScg");
  source.main->colorspace.scene_linear_to_xyz = ocio::ACESCG_TO_XYZ;
  const auto library = QDir(directory).filePath("authored.blend").toUtf8();
  STRNCPY(source.main->filepath, library.constData());
  auto &tree = *source.material->nodetree;
  auto &texture = image_node(tree);
  auto &img = *reinterpret_cast<Image *>(texture.id);
  if (tiled) {
    img.source = IMA_SRC_TILED;
    const auto reference = (relative ? QString("//source.<UDIM>.exr") :
                                       QDir(directory).filePath("source.<UDIM>.exr"))
                               .toUtf8();
    STRNCPY(img.filepath, reference.constData());
    for (const int tile : numbers) {
      auto *member = BKE_image_get_tile(&img, tile);
      if (!member)
        member = BKE_image_add_tile(&img, tile, "");
      require(member, "Cannot establish sparse Image tile table");
      BLI_strncpy(member->label,
                  QString("Authored tile %1").arg(tile).toUtf8().constData(),
                  sizeof(member->label));
    }
  }
  if (ordinary)
    for (auto &node : tree.nodes)
      if (STREQ(node.idname, "ShaderNodeBsdfPrincipled"))
        bke::node_add_link(tree,
                           texture,
                           socket(texture, "Color", false),
                           node,
                           socket(node, "Base Color", true));
  BKE_ntree_update_after_single_tree_change(*source.main, tree);
  return finish_native_material_source(
      std::move(source), cancel, nullptr, NativeWorldNormalSurfaceContract::PainterUndisplaced);
}
inline QJsonObject state(const NativeMaterialGraph &source, bool literal_path = true)
{
  auto result = native_fragment_retained_image_test::settings(source);
  QJsonArray images;
  for (const auto &img : source.main->images) {
    QJsonArray tiles;
    for (const auto &tile : img.tiles)
      tiles.append(QJsonObject{{"number", tile.tile_number},
                               {"label", tile.label},
                               {"gen_x", tile.gen_x},
                               {"gen_y", tile.gen_y},
                               {"gen_flag", int(tile.gen_flag)}});
    QJsonObject row{{"name", img.id.name + 2},
                    {"source", int(img.source)},
                    {"type", int(img.type)},
                    {"alpha", int(img.alpha_mode)},
                    {"space", img.colorspace_settings.name},
                    {"tiles", tiles}};
    if (literal_path)
      row["reference"] = img.filepath;
    images.append(row);
  }
  result["authored_images"] = images;
  result["recorded_space"] = source.main->colorspace.scene_linear_name;
  if (literal_path)
    result["source_Main_path"] = source.main->filepath;
  return result;
}
inline std::shared_ptr<const NativeMaterialImageTileSet> inventory(
    const NativeMaterialGraph &source)
{
  require(source.image_resources.empty() && source.tiled_image_resources.size() == 1,
          "TILED worker published single-image or incomplete inventories");
  const auto &entry = source.tiled_image_resources.front();
  require(entry.first && entry.first->source == blender::IMA_SRC_TILED && entry.second &&
              entry.second->tiles().size() == 4 && !entry.second->key().isEmpty(),
          "TILED inventory lost its exact Image/set owner");
  return entry.second;
}
inline void equal(const NativeMaterialImageTileSet &a,
                  const NativeMaterialImageTileSet &b,
                  bool shared)
{
  require(a.key() == b.key() && a.tiles().size() == b.tiles().size(),
          "Encoded tile-set identity changed on restore");
  for (const int tile : numbers) {
    const auto &x = a.tiles().at(tile), &y = b.tiles().at(tile);
    require(x && y && x->key() == y->key(), "Per-tile source identity changed");
    const auto *p = x->buffer(), *q = y->buffer();
    require(p && q && p->x == q->x && p->y == q->y && p->channels == 4 && q->channels == 4 &&
                p->float_data() && q->float_data(),
            "Restored tile payload dimensions/layout changed");
    require(std::memcmp(p->float_data(),
                        q->float_data(),
                        std::size_t(p->x) * p->y * 4 * sizeof(float)) == 0,
            "Restored normalized tile components changed");
    if (shared)
      require(x == y && p->float_data() == q->float_data(),
              "Independent Mains duplicated immutable tile pixels");
  }
}
inline void oracle(NativeMaterialGraph &source,
                   const QString &directory,
                   const char *space,
                   const std::atomic_bool &cancel,
                   bool raw)
{
  const auto set = inventory(source);
  require(!blender::BKE_image_has_loaded_ibuf(&image(source)),
          "TILED worker populated the original BKE Image cache");
  bool color_changed = false;
  for (std::size_t i = 0; i < numbers.size(); ++i) {
    auto file = graph(directory, space, cancel, false, numbers[i]);
    bool reused = false;
    const auto reference = prepare_shared_native_image(*file->main, image(*file), cancel, reused);
    const auto &member = set->tiles().at(numbers[i]);
    const auto *a = member->buffer(), *b = reference->buffer();
    require(a && a->float_data() && a->x == dimensions[i][0] && a->y == dimensions[i][1] &&
                a->channels == 4 && b && b->float_data() && a->x == b->x && a->y == b->y,
            "TILED/FILE decoder dimensions differ");
    require(std::memcmp(a->float_data(),
                        b->float_data(),
                        std::size_t(a->x) * a->y * 4 * sizeof(float)) == 0,
            "TILED normalization differs from equivalent explicit FILE decode");
    require(member == reference && a->float_data() == b->float_data(),
            "Equivalent FILE/TILED sources duplicated immutable normalized pixels");
    const auto independent = decode_native_material_image(*file->main, image(*file), cancel);
    require(independent.buffer && std::memcmp(a->float_data(),
                                              independent.buffer->float_data(),
                                              std::size_t(a->x) * a->y * 4 * sizeof(float)) == 0,
            "TILED normalization differs from independent FILE decode");
    const auto decoded = decode_native_material_image_tile(
        *source.main, image(source), numbers[i], cancel);
    require(decoded.buffer && decoded.buffer->x == a->x && decoded.buffer->y == a->y &&
                std::memcmp(a->float_data(),
                            decoded.buffer->float_data(),
                            std::size_t(a->x) * a->y * 4 * sizeof(float)) == 0,
            "Explicit tile decoder and shared inventory differ");
    for (int y = 0; y < a->y; ++y)
      for (int x = 0; x < a->x; ++x) {
        const auto expected = pixel(int(i), x, y);
        const auto *value = a->float_data() + 4 * (y * a->x + x);
        require(value[3] == expected[3], "TILED decode changed authored alpha, including zero");
        for (int c = 0; c < 3; ++c) {
          if (raw)
            require(value[c] == expected[c],
                    "Data TILED decode changed signed/HDR/alpha-zero RGB");
          else
            color_changed |= std::abs(value[c] - expected[c]) > 1e-4f;
        }
      }
  }
  require(raw || color_changed,
          "Non-gray Linear Rec.709 fixture never exercised ACEScg conversion");
}
inline int version(const QByteArray &bytes)
{
  require(bytes.size() >= 24, "Missing typed bundle header");
  const auto length = qFromBigEndian<quint64>(bytes.constData() + 8);
  return QJsonDocument::fromJson(bytes.sliced(24, qsizetype(length))).object()["version"].toInt();
}
inline void tiled_reference(const NativeMaterialImageReference &reference,
                            const StudioAsset &asset)
{
  require(reference.source == NativeMaterialImageSourceKind::Tiled &&
              reference.tiles.size() == 4 && asset.texture_files.size() == 4 &&
              asset.bytes.isEmpty() && reference.sha256 == asset.hash() &&
              reference.sha256 == texture_sources_hash(reference.tiles),
          "v3 tiled aggregate carrier identity/count changed or duplicated encoded bytes");
  for (std::size_t i = 0; i < numbers.size(); ++i) {
    const auto &a = reference.tiles[i], &b = asset.texture_files[i];
    require(a.tile == numbers[i] && a.path.isEmpty() && a.filename == b.filename &&
                a.sha256 == b.sha256 && a.size == b.size && b.size == QFileInfo(b.path).size() &&
                hash_texture_file(b.path) == b.sha256,
            "Portable tile reference lost exact number/filename/hash/size or leaked disk paths");
  }
}
inline bool metadata_only_guard(const QByteArray &wire)
{
#if defined(__linux__)
  const auto page = ::sysconf(_SC_PAGESIZE);
  require(page > 0, "Cannot determine owned guard page size");
  const auto n = qFromBigEndian<quint64>(wire.constData() + 8);
  auto metadata = wire.sliced(24, qsizetype(n));
  require(metadata.size() < page - 24, "Tiled manifest exceeds guard fixture page");
  metadata.append(page - 24 - metadata.size(), ' ');
  auto *memory = static_cast<char *>(
      ::mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  require(memory != MAP_FAILED, "Cannot allocate owned metadata guard pages");
  const auto free = qScopeGuard([&] { ::munmap(memory, 2 * page); });
  std::memcpy(memory, "MFPNAT1\n", 8);
  qToBigEndian<quint64>(metadata.size(), memory + 8);
  qToBigEndian<quint64>(page, memory + 16);
  std::memcpy(memory + 24, metadata.constData(), metadata.size());
  require(::mprotect(memory + page, page, PROT_NONE) == 0, "Cannot protect owned payload page");
  const auto guarded = QByteArray::fromRawData(memory, 2 * page);
  require(native_material_bundle_preview(guarded) == native_material_bundle_preview(wire),
          "Manifest-only preview changed");
  const auto expected = native_material_bundle_provenance(wire),
             actual = native_material_bundle_provenance(guarded);
  require(bool(actual) == bool(expected) &&
              (!actual || native_image_variant_provenance_json(*actual) ==
                              native_image_variant_provenance_json(*expected)),
          "Manifest-only provenance changed");
  const auto refs = native_material_bundle_image_manifest(guarded),
             expected_refs = native_material_bundle_image_manifest(wire);
  require(refs.size() == expected_refs.size() && refs.size() == 1 &&
              refs.front().image == expected_refs.front().image &&
              refs.front().sha256 == expected_refs.front().sha256 &&
              texture_sources_json(refs.front().tiles, false) ==
                  texture_sources_json(expected_refs.front().tiles, false),
          "Manifest-only typed references changed");
  return true;
#else
  (void)wire;
  return false;
#endif
}
inline QJsonObject transports(const NativeMaterialArchive &archive)
{
  const auto bundle = read_native_material_bundle(archive.bytes);
  require(version(archive.bytes) == 3 && bundle.images.size() == 1 && archive.textures.size() == 1,
          "TILED external archive did not use one typed v3 reference/carrier");
  tiled_reference(bundle.images.front(), archive.textures.front());
  NativeImageBinding binding{{},
                             101,
                             202,
                             "Color",
                             "Color1",
                             "ShaderNodeMixRGB",
                             "CPU transport lineage",
                             {{"image_source", 1},
                              {"image_type", 0},
                              {"alpha_mode", 0},
                              {"high_precision", true},
                              {"multiview", false},
                              {"source_space", "sRGB"},
                              {"interpolation", 1},
                              {"projection", 0},
                              {"projection_blend", 0.},
                              {"extension", 0}}};
  const NativeImageVariantProvenance lineage{
      QUuid("{ca358cbd-e34a-4ea0-a82b-2a811f879465}"),
      QString::fromLatin1(
          QCryptographicHash::hash(archive.bytes, QCryptographicHash::Sha256).toHex()),
      {{binding, QString(64, 'b'), "sRGB", "original-declared"}}};
  int count = 0;
  bool guarded_payload = false;
  for (bool variant : {false, true}) {
    const std::optional<NativeImageVariantProvenance> provenance = variant ?
                                                                       std::optional(lineage) :
                                                                       std::nullopt;
    const auto plain = native_material_bundle_bytes(bundle.blend, bundle.images, provenance);
    const auto preview = native_material_bundle_bytes(
        bundle.blend, bundle.images, provenance, QByteArray("CPU preview A"));
    if (variant)
      guarded_payload = metadata_only_guard(preview);
    const auto decoded = read_native_material_bundle(preview);
    const auto rewritten = native_material_bundle_bytes(decoded.blend,
                                                        decoded.images,
                                                        native_material_bundle_provenance(preview),
                                                        QByteArray("CPU preview B"));
    require(native_material_source_hash(plain) == native_material_source_hash(preview) &&
                native_material_source_hash(preview) == native_material_source_hash(rewritten) &&
                native_material_bundle_preview(preview) == QByteArray("CPU preview A") &&
                native_material_bundle_preview(rewritten) == QByteArray("CPU preview B"),
            "v3 preview changed source identity or payload");
    for (const auto &bytes : {plain, preview, rewritten}) {
      const auto read = read_native_material_bundle(bytes);
      require(version(bytes) == 3 && read.blend == bundle.blend && read.images.size() == 1,
              "v3 transport changed actual Blender payload");
      tiled_reference(read.images.front(), archive.textures.front());
      const auto actual = native_material_bundle_provenance(bytes);
      require(bool(actual) == variant &&
                  (!actual || native_image_variant_provenance_json(*actual) ==
                                  native_image_variant_provenance_json(lineage)),
              "v3 rewrite changed variant lineage");
      ++count;
    }
  }
  return {{"typed_version", 3},
          {"metadata_only_protected_payload_probe", guarded_payload},
          {"image_references", 1},
          {"aggregate_carriers", 1},
          {"encoded_members", 4},
          {"rewritten_transports", count},
          {"exact_payload_tiles_and_lineage", true},
          {"preview_source_hash_unchanged", true},
          {"encoded_bytes_not_duplicated_in_bundle", true}};
}
inline bool rejects(auto function)
{
  try {
    function();
  }
  catch (const std::exception &) {
    return true;
  }
  return false;
}
}  // namespace bts::native_tiled_source_test

namespace bts {
inline QJsonObject verify_native_material_tiled_sources()
{
  using namespace native_tiled_source_test;
  require(QCoreApplication::instance() &&
              QThread::currentThread() == QCoreApplication::instance()->thread(),
          "Tiled source fixture requires initialized CPU owner");
  require(!blender::GPU_context_active_get(), "Tiled source fixture entered with GPU context");
  const auto compilations = blender::GPU_pass_global_compilation_count();
  std::atomic_bool cancel = false;
  QTemporaryDir directory;
  require(directory.isValid(), "Cannot create owned tiled source directory");
  const auto proof = work([&] {
    write(directory.path());
    QJsonArray policies;
    QJsonObject original_controls;
    NativeMaterialArchive external, packed;
    std::vector<std::weak_ptr<NativeMaterialImageResource>> expired;
    std::weak_ptr<const NativeMaterialImageTileSet> expired_set;
    for (const char *space : {"Non-Color", "Linear Rec.709"}) {
      auto a = graph(directory.path(), space, cancel), b = graph(directory.path(), space, cancel);
      const auto before = state(*a);
      const auto rows = prepare_native_material_images(*a, cancel);
      require(rows.size() == 4, "TILED worker did not report exactly four source members");
      oracle(*a, directory.path(), space, cancel, std::string_view(space) == "Non-Color");
      auto owners = inventory(*a);
      const auto decodes = native_material_image_pool_statistics().decodes;
      prepare_native_material_images(*b, cancel);
      const auto shared = inventory(*b);
      require(owners == shared && native_material_image_pool_statistics().decodes == decodes,
              "Second Main duplicated a complete tiled owner");
      equal(*owners, *shared, true);
      require(state(*a) == before && !blender::BKE_image_has_loaded_ibuf(&image(*a)) &&
                  !blender::BKE_image_has_loaded_ibuf(&image(*b)),
              "TILED worker changed source Main, tile table, controls or cache");
      if (std::string_view(space) == "Non-Color") {
        original_controls = state(*a, false);
        external = archive_native_material(
            *a, QDir(directory.path()).filePath("external.blend"), cancel, true);
        packed = archive_native_material(
            *a, QDir(directory.path()).filePath("packed.blend"), cancel, false);
        require(state(*a) == before && image(*a).packedfiles.is_empty(),
                "Archive packed/coerced/edited original TILED source");
        require(read_native_material_bundle(packed.bytes).images.empty(),
                "Packed tiled archive invented external references");
        validate_native_material_tiled_sources(
            *a, native_material_bundle_image_manifest(external.bytes), cancel);
        expired_set = owners;
        for (const auto &[tile, member] : owners->tiles())
          expired.push_back(member);
      }
      policies.append(QJsonObject{{"source_space", space},
                                  {"graph_space", "ACEScg"},
                                  {"tiles", 4},
                                  {"per_FILE_oracle_exact", true},
                                  {"per_FILE_resource_and_pixel_shared", true},
                                  {"explicit_tile_decoder_exact", true},
                                  {"shared_set_and_pixels_across_Mains", true},
                                  {"original_BKE_cache_unpopulated", true},
                                  {"source_controls_links_tiles_preserved", true}});
    }
    require(expired_set.expired() && expired.size() == 4 &&
                std::all_of(expired.begin(),
                            expired.end(),
                            [](const auto &entry) { return entry.expired(); }),
            "Tiled set/pixels remained strongly retained after source Main destruction");
    const auto transport = transports(external);
    // Deterministic archive/decode fencing: the source changes between these
    // actual production phases, without timing or a racing thread.
    {
      QTemporaryDir changed_directory;
      require(changed_directory.isValid(), "Cannot create owned source-coherence directory");
      write(changed_directory.path());
      auto changed = graph(changed_directory.path(), "Non-Color", cancel);
      const auto before = state(*changed);
      const auto saved = archive_native_material(
          *changed, changed_directory.filePath("snapshot.blend"), cancel, true);
      const auto references = native_material_bundle_image_manifest(saved.bytes);
      write(changed_directory.path(), numbers.back());
      prepare_native_material_images(*changed, cancel);
      require(inventory(*changed)->tiles().at(numbers.back())->encoded_sha256() !=
                  references.front().tiles.back().sha256,
              "Source rewrite did not change the actual decoded encoded digest");
      bool mismatch = false;
      try {
        validate_native_material_tiled_sources(*changed, references, cancel);
      }
      catch (const std::runtime_error &error) {
        const auto message = QString::fromUtf8(error.what());
        mismatch = message.contains("tile 2000") &&
                   message.contains("decoded source differs from immutable archive bytes");
      }
      require(mismatch && state(*changed) == before,
              "Archive/decode source mismatch was published or changed the source graph");
    }
    // Legacy FILE transports keep the established raw/v1/v2 compatibility oracle.
    auto file = graph(directory.path(), "Non-Color", cancel, false);
    auto legacy_archive = archive_native_material(
        *file, QDir(directory.path()).filePath("legacy-file.blend"), cancel, true);
    require(version(legacy_archive.bytes) == 1,
            "Unchanged FILE transport was needlessly promoted to v3");
    const auto legacy = verify_native_material_source_bundle_compatibility(legacy_archive.bytes);
    file.reset();
    // Fail only the last member, after earlier members are available; no partial
    // complete-set inventory may escape. This Main is retired before cold read.
    auto missing = graph(directory.path(), "Non-Color", cancel);
    const auto missing_before = state(*missing);
    const auto live_before_failure = native_material_image_pool_statistics().live_resources;
    const auto last = path(directory.path(), numbers.back());
    require(QFile::rename(last, last + ".missing"), "Cannot hide last source tile");
    {
      const auto restore_last = qScopeGuard([&] { QFile::rename(last + ".missing", last); });
      require(rejects([&] { prepare_native_material_images(*missing, cancel); }) &&
                  missing->tiled_image_resources.empty() && state(*missing) == missing_before,
              "Missing last source tile published a partial owner or changed source");
    }
    require(native_material_image_pool_statistics().live_resources == live_before_failure,
            "Failed last member retained partial decoded owners");
    missing.reset();
    auto optional_missing = graph(directory.path(), "Non-Color", cancel, true, 1001, false, true);
    prepare_optional_native_world_normal(optional_missing, cancel);
    auto required_missing = graph(directory.path(), "Non-Color", cancel);
    QStringList hidden;
    const auto restore = qScopeGuard([&] {
      for (const auto &name : hidden)
        QFile::rename(name + ".hidden", name);
    });
    for (const int tile : numbers) {
      const auto name = path(directory.path(), tile);
      require(QFile::rename(name, name + ".hidden"), "Cannot hide original TILED source");
      hidden.append(name);
    }
    auto asset = native_material_asset(
             "External tiled CPU source", external.bytes, external.description),
         packed_asset = native_material_asset(
             "Packed tiled CPU source", packed.bytes, packed.description);
    asset.scope = AssetScope::Project;
    packed_asset.scope = AssetScope::Project;
    auto a = hydrate_native_material_resource(asset, cancel, external.textures),
         b = hydrate_native_material_resource(packed_asset, cancel);
    const auto owners = inventory(*a), other = inventory(*b);
    require(owners == other, "Cold external/packed sources failed immutable sharing");
    equal(*owners, *other, true);
    require(state(*a, false) == original_controls && state(*b, false) == original_controls &&
                image(*a).source == blender::IMA_SRC_TILED &&
                image(*b).source == blender::IMA_SRC_TILED && !image(*b).packedfiles.is_empty(),
            "Cold archive changed original source kind, controls or packed tile ownership");
    std::set<int> packed_numbers;
    for (const auto &member : image(*b).packedfiles)
      packed_numbers.insert(member.tile_number);
    require(packed_numbers == std::set<int>(numbers.begin(), numbers.end()),
            "Packed archive lost sparse tile IDs");
    const auto encoded_bundle = read_native_material_bundle(external.bytes);
    for (const auto &member : image(*b).packedfiles) {
      const auto &encoded = encoded_bundle.images.front().tiles;
      const auto expected = std::find_if(encoded.begin(), encoded.end(), [&](const auto &tile) {
        return tile.tile == member.tile_number;
      });
      require(expected != encoded.end() && member.packedfile &&
                  member.packedfile->size == expected->size &&
                  QString::fromLatin1(
                      QCryptographicHash::hash(
                          QByteArrayView(static_cast<const char *>(member.packedfile->data),
                                         member.packedfile->size),
                          QCryptographicHash::Sha256)
                          .toHex()) == expected->sha256,
              "Packed tile changed original encoded bytes");
    }
    require(!blender::BKE_image_has_loaded_ibuf(&image(*a)) &&
                !blender::BKE_image_has_loaded_ibuf(&image(*b)),
            "Cold worker mutated BKE Image cache");
    for (std::size_t i = 0; i < numbers.size(); ++i) {
      const auto *buffer = owners->tiles().at(numbers[i])->buffer();
      require(buffer && buffer->x == dimensions[i][0] && buffer->y == dimensions[i][1] &&
                  buffer->float_data(),
              "Cold payload lost mixed rectangular source sizes");
      for (int y = 0; y < buffer->y; ++y)
        for (int x = 0; x < buffer->x; ++x) {
          const auto expected = pixel(int(i), x, y);
          for (int c = 0; c < 4; ++c)
            require(buffer->float_data()[4 * (y * buffer->x + x) + c] == expected[c],
                    "Cold source-free tiled raw pixels changed");
        }
    }
    NativeMaterialGpuLibrary library;
    library.install("tiled-source-cpu", asset.hash().toStdString(), a);
    require(library.statistics().programs == 0 && library.statistics().world_programs == 0,
            "TILED source CPU install created a program");
    // Real binding failures must validate every member before changing any Image.
    auto bundle = read_native_material_bundle(external.bytes);
    auto raw = asset;
    raw.bytes = bundle.blend;
    int atomic_rejections = 0;
    const auto binding_failure = [&](auto mutate) {
      auto detached = read_native_material_source(
          materialize_native_material_archive(raw, cancel), a->description.material, cancel);
      auto &target = *static_cast<blender::Image *>(detached.main->images.first);
      const auto before = QString::fromUtf8(target.filepath);
      std::vector<int> tiles;
      for (const auto &tile : target.tiles)
        tiles.push_back(tile.tile_number);
      auto invalid = bundle;
      auto textures = external.textures;
      mutate(invalid, textures);
      require(rejects([&] {
                bind_native_material_source_images(*detached.main, invalid, textures, cancel);
              }),
              "Malformed tiled binding unexpectedly succeeded");
      std::vector<int> after;
      for (const auto &tile : target.tiles)
        after.push_back(tile.tile_number);
      require(QString::fromUtf8(target.filepath) == before && after == tiles &&
                  target.source == blender::IMA_SRC_TILED,
              "Failed tiled binding partially changed source Image");
      ++atomic_rejections;
    };
    binding_failure([](auto &, auto &textures) { textures.clear(); });
    binding_failure([](auto &invalid, auto &textures) {
      auto &reference = invalid.images.front();
      reference.tiles.pop_back();
      reference.sha256 = texture_sources_hash(reference.tiles);
      textures.front().texture_files.pop_back();
    });
    binding_failure([](auto &invalid, auto &) {
      auto &reference = invalid.images.front();
      reference.tiles[1].tile = reference.tiles[0].tile;
    });
    binding_failure([&](auto &, auto &textures) {
      auto &member = textures.front().texture_files.front();
      QFile input(member.path);
      require(input.open(QIODevice::ReadOnly), "Cannot read owned encoded snapshot");
      auto bytes = input.readAll();
      require(!bytes.isEmpty(), "Empty owned snapshot");
      bytes[bytes.size() - 1] = char(bytes.back() ^ 1);
      const auto corrupt = directory.filePath("corrupt-tile.exr");
      QFile output(corrupt);
      require(output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size(),
              "Cannot create owned checksum-negative file");
      output.close();
      member.path = corrupt;
    });
    int packed_set_rejections = 0;
    {
      auto &img = image(*b);
      auto &part = *static_cast<blender::ImagePackedFile *>(img.packedfiles.last);
      const int old_number = part.tile_number;
      part.tile_number = numbers[0];
      require(rejects([&] { native_material_encoded_image_source(*b->main, img, numbers[0]); }),
              "Duplicate packed tile was admitted");
      ++packed_set_rejections;
      part.tile_number = old_number;
      const int old_view = part.view;
      part.view = 1;
      require(rejects([&] { native_material_encoded_image_source(*b->main, img, numbers[0]); }),
              "Nondefault packed view was admitted as a single-view TILED source");
      ++packed_set_rejections;
      part.view = old_view;
      blender::BLI_remlink(&img.packedfiles, &part);
      const auto relink = qScopeGuard([&] { blender::BLI_addtail(&img.packedfiles, &part); });
      require(rejects([&] { native_material_encoded_image_source(*b->main, img, numbers[0]); }),
              "Partial packed set silently fell back to external source");
      ++packed_set_rejections;
    }
    int manifest_rejections = 0;
    {
      const auto length = qFromBigEndian<quint64>(external.bytes.constData() + 8);
      const auto descriptor =
          QJsonDocument::fromJson(external.bytes.sliced(24, qsizetype(length))).object();
      const auto reject_manifest = [&](auto mutate) {
        auto changed = descriptor;
        auto refs = changed["images"].toArray();
        auto row = refs[0].toObject();
        mutate(row);
        refs[0] = row;
        changed["images"] = refs;
        const auto metadata = QJsonDocument(changed).toJson(QJsonDocument::Compact);
        QByteArray wire("MFPNAT1\n", 8);
        char sizes[16];
        qToBigEndian<quint64>(metadata.size(), sizes);
        qToBigEndian<quint64>(bundle.blend.size(), sizes + 8);
        wire.append(sizes, 16);
        wire.append(metadata);
        wire.append(bundle.blend);
        require(rejects([&] { read_native_material_bundle(wire); }),
                "Malformed v3 tiled manifest was admitted");
        ++manifest_rejections;
      };
      reject_manifest([](auto &row) { row["source"] = "FILE"; });
      reject_manifest([](auto &row) { row["filename"] = "without-token.exr"; });
      reject_manifest([](auto &row) { row["sha256"] = QString(64, '0'); });
      reject_manifest([](auto &row) {
        auto tiles = row["tiles"].toArray();
        auto tile = tiles[1].toObject();
        tile["tile"] = 1001;
        tiles[1] = tile;
        row["tiles"] = tiles;
      });
      reject_manifest([](auto &row) {
        auto tiles = row["tiles"].toArray();
        auto tile = tiles[0].toObject();
        tile["filename"] = "../escape.exr";
        tiles[0] = tile;
        row["tiles"] = tiles;
      });
      reject_manifest([](auto &row) {
        auto tiles = row["tiles"].toArray();
        auto tile = tiles[0].toObject();
        tile["path"] = "/untrusted";
        tiles[0] = tile;
        row["tiles"] = tiles;
      });
    }
    std::atomic_bool stopped = true;
    const auto before_inventory = a->tiled_image_resources;
    bool reused = false;
    require(rejects([&] {
              prepare_shared_native_tiled_image(*a->main, image(*a), stopped, reused);
            }) &&
                rejects([&] { prepare_native_material_images(*a, stopped); }) &&
                a->tiled_image_resources == before_inventory,
            "Cancelled worker published/replaced a tiled inventory");
    int missing_optional_cases = 0;
    {
      auto optional = optional_missing;
      const auto before = state(*optional);
      const auto expected_pattern = native_material_tiled_image_pattern(*optional->main,
                                                                        image(*optional));
      for (bool external_images : {true, false}) {
        auto saved = archive_native_material(*optional,
                                             directory.filePath(external_images ?
                                                                    "optional-external.blend" :
                                                                    "optional-packed.blend"),
                                             cancel,
                                             external_images);
        require(saved.textures.empty() &&
                    read_native_material_bundle(saved.bytes).images.empty() &&
                    saved.diagnostics["missing_optional_normal_images"].toArray().size() == 1 &&
                    saved.diagnostics["missing_unused_images"].toArray().isEmpty(),
                "Unavailable optional TILED source was mislabeled or published partial resources");
        require(saved.description.warnings.join(" ").contains("Bump shading image is unavailable"),
                "Optional TILED source lacks its precise Bump warning");
        auto asset = native_material_asset(
            "Missing optional TILED", saved.bytes, saved.description);
        asset.scope = AssetScope::Project;
        auto reopened = hydrate_native_material_resource(asset, cancel);
        require(
            image(*reopened).source == blender::IMA_SRC_TILED &&
                QString::fromUtf8(image(*reopened).filepath) == "//source.<UDIM>.exr" &&
                native_material_tiled_image_pattern(*reopened->main, image(*reopened)) ==
                    expected_pattern &&
                reopened->tiled_image_resources.empty() && image(*reopened).packedfiles.is_empty(),
            "Optional missing TILED archive lost exact pattern/origin or invented decoded pixels");
        ++missing_optional_cases;
      }
      require(state(*optional) == before,
              "Optional missing archive changed the original TILED source");
      auto required = required_missing;
      const auto required_before = state(*required);
      require(rejects([&] {
                archive_native_material(
                    *required, directory.filePath("required-missing.blend"), cancel, true);
              }) &&
                  state(*required) == required_before,
              "Missing required TILED source did not fail without mutation");
    }
    QJsonArray tile_numbers, tile_dimensions;
    for (std::size_t i = 0; i < numbers.size(); ++i) {
      tile_numbers.append(numbers[i]);
      tile_dimensions.append(QJsonArray{dimensions[i][0], dimensions[i][1]});
    }
    return QJsonObject{{"sparse_tiles", tile_numbers},
                       {"dimensions", tile_dimensions},
                       {"signed_HDR_alpha_zero_preserved", true},
                       {"policies", policies},
                       {"external_v3", transport},
                       {"legacy_FILE", legacy},
                       {"source_Main_destroyed_before_cold_hydrate", true},
                       {"expired_tile_sets", 1},
                       {"expired_decoded_owners", 4},
                       {"hidden_originals", hidden.size()},
                       {"cold_external_and_packed_pixels_exact", true},
                       {"packed_sparse_tile_IDs_preserved", true},
                       {"cold_shared_resource_and_pixel_identity", true},
                       {"atomic_binding_rejections", atomic_rejections},
                       {"malformed_v3_manifest_rejections", manifest_rejections},
                       {"strict_packed_set_rejections", packed_set_rejections},
                       {"optional_missing_pattern_origin_cases", missing_optional_cases},
                       {"missing_required_archive_rejected", true},
                       {"packed_encoded_tile_bytes_exact", true},
                       {"post_archive_source_rewrite_rejected_before_publication", true},
                       {"typed_encoded_digests_preserved", true},
                       {"pre_cancelled_factory_and_worker_rejections", 2},
                       {"missing_source_partial_inventory_rejected", true},
                       {"programs", 0},
                       {"world_programs", 0}};
  });
  require(!blender::GPU_context_active_get() &&
              blender::GPU_pass_global_compilation_count() == compilations,
          "TILED source fixture created a GPU context/program");
  return {
      {"complete", true},
      {"scope",
       "CPU encoded tiled source archives, explicit graph-space workers, immutable ownership and atomic binding; no sampler or uploader execution"},
      {"sources", proof},
      {"gpu_context_created", false},
      {"gpu_compilations", 0}};
}
}  // namespace bts
