#define main existing_height_policy_full_main
#include "native_height_policy_native_test.cc"
#undef main
#include "NativeMaterialSourceLoader.hh"
#include "NativeMaterialBundle.hh"
int main(int argc, char **argv)
{
  using namespace height_policy_owner;
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QSurfaceFormat format;
  format.setVersion(4, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setDepthBufferSize(24);
  QSurfaceFormat::setDefaultFormat(format);
  initialize_numeric_locale();
  QApplication app(argc, argv);
  try {
    require(argc == 2, "Expected private synthetic Height output");
    const QString directory = QString::fromLocal8Bit(argv[1]);
    Harness h(directory);
    h.create();
    auto source = synthetic_source(directory, Kind::Affine);
    const auto before_hash = source.asset.hash();
    source.graph->resource_content_hash = work(
        [&] { return native_material_source_hash(source.asset.bytes); });
    source.graph->generate_resource_preview = true;
    QString failure;
    NativeMaterialSourceLoader loader(h.host, nullptr, [] {}, [](const QString &) {});
    PreparedBlendMaterial prepared;
    prepared.material = source.asset;
    prepared.native_graph = source.graph;
    loader.prepared(
        std::move(prepared),
        [&](const StudioAsset &published) {
          source.asset = published;
          h.resources.push_back(published);
        },
        [&](const QString &error) { failure = error; });
    h.wait([&] { return !loader.pending(); }, "Preview/Height source did not publish");
    require(failure.isEmpty(), qPrintable(failure));
    require(source.asset.hash() != before_hash && !blend_material_preview(source.asset).isNull(),
            "Height alias fixture did not change transport identity with its preview");
    const auto none = h.apply(source.asset), raw = h.apply(source.asset);
    h.activate(source.asset, raw);
    h.source_none(source.asset, none, raw);
    h.publish();
    const auto affine = verify_stack(h, .5, 1), constant = verify_constant_transport(h, raw);
    QDir().mkpath(directory + "/foreign");
    auto foreign = synthetic_source(directory + "/foreign", Kind::Constant, .37f);
    h.install(foreign.asset, foreign.graph);
    auto wrong = work([&] {
      std::atomic_bool cancelled = false;
      return prepare_native_material_height_resource(foreign.asset, h.resources, cancelled);
    });
    bool rejected = false;
    try {
      auto candidate = h.host.stage_native_height_gpu(source.asset.id, std::move(wrong));
    }
    catch (const std::invalid_argument &error) {
      rejected = QString::fromUtf8(error.what()).contains("does not belong");
    }
    require(rejected, "Height accepted another source through preview alias identity");
    // Detached graph owners must die while Blender's allocator/types are alive.
    source.graph.reset();
    foreign.graph.reset();
    h.shutdown();
    save_json(directory + "/verified.json",
              {{"preview_only_hash_alias_accepted", true},
               {"different_source_height_alias_rejected", true},
               {"affine_two_owner_two_tile", affine},
               {"constant_raw_transport", constant}});
    std::puts("HEIGHT_PREVIEW_ALIAS_SYNTHETIC PASS");
    return 0;
  }
  catch (const std::exception &error) {
    std::fprintf(stderr, "HEIGHT_PREVIEW_ALIAS_SYNTHETIC_FAILED %s\n", error.what());
    return 1;
  }
}
