// SPDX-License-Identifier: GPL-3.0-or-later
#include "RequestedFeaturesProof.hh"
#include "AssetCatalog.hh"
#include "AssetCategories.hh"
#include "AssetThumbnail.hh"
#include "ResourceImport.hh"
#include "ImageSequence.hh"
#include "FillTextureIO.hh"
#include "TextureExport.hh"
#include "ExportConfiguration.hh"
#include "WorkspaceShell.hh"
#include "CompareMaskPanel.hh"
#include "FillPanel.hh"
#include <OpenImageIO/imageio.h>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QToolButton>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QUrl>
#include <set>
namespace bts {
namespace {
void require(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(std::string("Requested feature proof: ") + message);
}
void events(int ms)
{
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < ms) {
    QApplication::processEvents();
    QThread::msleep(5);
  }
}
void write_fixture(const QString &path)
{
  const int size = 128;
  std::vector<float> pixels(size * size * 3);
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      const float h = .5f + .24f * std::sin(x * .073f + std::sin(y * .055f) * 2) +
                      .16f * std::cos(y * .11f);
      for (int c = 0; c < 3; ++c)
        pixels[(y * size + x) * 3 + c] = h;
    }
  auto out = OIIO::ImageOutput::create(path.toStdString());
  require(bool(out), "fixture encoder");
  OIIO::ImageSpec spec(
      size, size, 3, path.endsWith("exr") ? OIIO::TypeDesc::FLOAT : OIIO::TypeDesc::UINT8);
  spec.attribute("oiio:ColorSpace", "Linear");
  require(out->open(path.toStdString(), spec) &&
              out->write_image(OIIO::TypeDesc::FLOAT, pixels.data()) && out->close(),
          "fixture write");
}
StudioAsset source_asset(const QString &path)
{
  const auto source = reference_image_resource(path, false);
  StudioAsset asset;
  asset.filename = source.filename;
  asset.path = source.path;
  asset.name = source.name;
  asset.texture_files = source.files;
  asset.bytes = source.bytes;
  asset.scope = AssetScope::User;
  asset.usages = {"texture"};
  return asset;
}
QImage composite_image(const TextureSet &set)
{
  QImage image(set.width(), set.rows(), QImage::Format_RGB32);
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) {
      const auto value = TextureDocument::evaluate(set, x, y).material;
      int rgb[3];
      for (int c = 0; c < 3; ++c) {
        const float v = std::clamp(value.base_color[c], 0.f, 1.f);
        rgb[c] = qRound(255 *
                        (v <= .0031308f ? 12.92f * v : 1.055f * std::pow(v, 1 / 2.4f) - .055f));
      }
      image.setPixel(x, image.height() - 1 - y, qRgb(rgb[0], rgb[1], rgb[2]));
    }
  return image;
}
}  // namespace
int run_requested_features_proof(const QString &directory)
{
  require(QDir().mkpath(directory), "proof directory");
  const auto original = directory + "/Strata_Texture.png";
  write_fixture(original);
  // Exercise the actual Import dialog and its persistent Texture defaults.
  QTimer::singleShot(40, [] {
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    require(dialog, "Import dialog missing");
    require(dialog->findChild<QComboBox *>("resourceImportUsage")->currentIndex() == 1,
            "single images must default to Texture");
    require(dialog->findChild<QComboBox *>("resourceImportScope")->currentIndex() == 2,
            "default import must persist in User shelf");
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
  });
  auto imported = choose_resource_import(nullptr, false, original);
  require(bool(imported), "Import dialog failed");
  AssetCatalog catalog(directory + "/library");
  catalog.add_prepared(AssetCatalog::prepare_user(*imported, catalog.directory()));
  require(QFile::remove(original), "remove original");
  AssetCatalog reopened(catalog.directory());
  reopened.load_user_library();
  auto retained = reopened.assets().back();
  require(retained.id == imported->id && asset_in_category(retained, "Textures"),
          "persistent texture metadata");
  require(QFile::exists(retained.texture_files.front().path),
          "library must retain source after original removal");
  auto raw = decode_fill_texture(retained, true), color = decode_fill_texture(retained, false);
  require(raw->raw && !color->raw && raw->width == 128, "texture decoding");
  QJsonArray formats;
  for (const auto &extension : {"png", "jpg", "tif", "exr", "tga"}) {
    const auto path = directory + "/format." + extension;
    write_fixture(path);
    auto source = source_asset(path);
    catalog.add_prepared(AssetCatalog::prepare_user(source, catalog.directory()));
    QFile::remove(path);
    auto decoded = decode_fill_texture(catalog.assets().back(), true);
    require(decoded->width == 128 && decoded->height == 128, "format import/persistence/decode");
    formats.append(extension);
  }
  TextureSet set;
  set.name = "Strata";
  set.texture_resolution = 128;
  set.channels.fill(true);
  set.precision.fill(ChannelPrecision::RGB32F);
  set.uniform_editable = true;
  TextureDocument::add_fill(set);
  const auto lower = set.selected_layer;
  auto &base = TextureDocument::layer(set, lower);
  base.name = "Blue stone";
  base.values.base_color = {.025f, .16f, .4f, 1};
  base.values.bump = .5;
  TextureDocument::add_fill(set);
  const auto upper = set.selected_layer;
  auto &top = TextureDocument::layer(set, upper);
  top.name = "Copper strata";
  top.values.base_color = {.8f, .25f, .04f, 1};
  fill_channel_texture(top.fill, 4) = raw;
  // All five channel slots and mask Fill consume the same retained texture.
  for (int c = 0; c < 5; ++c) {
    auto probe = set;
    auto &layer = probe.layers.back();
    fill_channel_texture(layer.fill, c) = c == 0 ? color : raw;
    const auto value = TextureDocument::fill_sample(probe, layer, c, 47, 61, 1001);
    for (float v : value)
      require(std::isfinite(v), "channel texture sample");
    require(value[3] > 0, "channel texture coverage");
  }
  {
    auto probe = set;
    auto &layer = probe.layers.back();
    layer.mask = LayerMask{};
    MaskEffect fill;
    fill.kind = MaskEffectKind::Fill;
    fill.mapping.projection.texture = raw;
    layer.mask->effects = {fill};
    require(TextureDocument::mask_value(probe, layer.id, 47, 61) > .05f,
            "mask Fill texture input");
  }
  {
    FillPanel panel;
    std::array<bool, 15> enabled{};
    for (int c = 0; c < 5; ++c)
      enabled[c] = true;
    panel.set_channels(enabled, {"Base Color", "Metallic", "Roughness", "Normal", "Height"});
    panel.set_enabled_channels(enabled);
    panel.set_fill_context(true);
    panel.synchronize(top.fill);
    int received = -1;
    panel.asset_dropped = [&](int c, const QString &id) {
      require(QUuid(id) == retained.id, "channel asset identity");
      received = c;
    };
    QMimeData mime;
    mime.setData("application/x-bts-asset", retained.id.toString().toUtf8());
    for (int c = 0; c < 5; ++c) {
      auto *header = panel.findChild<QToolButton *>(c ? QString("fillChannelHeader%1").arg(c) :
                                                        QString("fillBaseColorHeader"));
      require(header && !header->isHidden(),
              "channel resource header, including Normal, must be exposed");
      QDragEnterEvent enter(QPoint(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(header, &enter);
      QDropEvent drop(QPointF(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(header, &drop);
      require(enter.isAccepted() && drop.isAccepted() && received == c,
              "asset drop into channel slot");
    }
  }
  TextureDocument::add_compare_mask(set);
  auto &compare = top.mask->effects.front();
  compare.compare_width = .12;
  compare.compare_offset = -.1;
  auto *view = new QLabel;
  view->setAlignment(Qt::AlignCenter);
  view->setMinimumSize(500, 360);
  WorkspaceShell::Callbacks callbacks;
  bool dropped = false;
  callbacks.texture_assets_import_requested = [&](const QStringList &paths) {
    dropped = paths == QStringList{retained.texture_files.front().path};
  };
  WorkspaceShell workspace(view, callbacks);
  workspace.resize(1500, 1050);
  workspace.set_asset_catalog(&reopened);
  workspace.set_texture_sets({"Strata"});
  NewProjectOptions settings;
  workspace.show_texture_set(&set, &settings);
  workspace.set_shelf_visible(true);
  workspace.show();
  workspace.show_texture_shelf();
  workspace.findChild<QLineEdit *>("shelfSearch")->setText("Strata_Texture");
  workspace.findChild<QComboBox *>("shelfLibraryScope")->setCurrentIndex(4);
  events(500);
  auto *assets = workspace.findChild<QListWidget *>("shelfAssets");
  require(assets && assets->count() == 1, "imported texture visible in Assets");
  QMimeData mime;
  mime.setUrls({QUrl::fromLocalFile(retained.texture_files.front().path)});
  QDragEnterEvent enter(QPoint(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(assets->viewport(), &enter);
  QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(assets->viewport(), &drop);
  require(enter.isAccepted() && drop.isAccepted() && dropped, "Assets file drop routing");
  QImage thumbnail;
  QElapsedTimer thumbnail_wait;
  thumbnail_wait.start();
  bool thumbnail_ready = false;
  while (thumbnail_wait.elapsed() < 10000) {
    thumbnail = asset_thumbnail(retained);
    std::set<QRgb> tones;
    for (int y = 0; y < thumbnail.height(); ++y)
      for (int x = 0; x < thumbnail.width(); ++x)
        tones.insert(thumbnail.pixel(x, y));
    if (tones.size() > 16) {
      thumbnail_ready = true;
      break;
    }
    events(20);
  }
  require(thumbnail_ready, "decoded texture thumbnail");
  require(thumbnail.save(directory + "/texture-thumbnail.png"), "thumbnail PNG");
  require(QFileInfo::exists(QFileInfo(retained.texture_files.front().path)
                                .dir()
                                .filePath(retained.hash() + ".thumbnail-v1.png")),
          "thumbnail persisted in library");
  for (int setting = 0; setting < 2; ++setting) {
    compare.compare_offset = setting ? .12f : -.1f;
    compare.compare_width = setting ? .55f : .12f;
    compare.compare_ramp = setting ? std::array<float, 5>{0, .04f, .35f, .9f, 1} :
                                     std::array<float, 5>{0, .25f, .5f, .75f, 1};
    ExportRequest blend_export;
    blend_export.directory = directory.toStdString();
    blend_export.selected_channels = std::array<bool, 9>{
        true, false, false, false, false, false, false, false, false};
    blend_export.naming[0] = "compare-export-" + std::to_string(setting + 1);
    ExportTile blend_tile;
    blend_tile.set = set;
    blend_tile.resolution = 128;
    blend_tile.normal.resize(128 * 128 * 4);
    for (int i = 0; i < 128 * 128; ++i) {
      blend_tile.normal[i * 4] = blend_tile.normal[i * 4 + 1] = .5f;
      blend_tile.normal[i * 4 + 2] = blend_tile.normal[i * 4 + 3] = 1;
    }
    blend_export.tiles = {blend_tile};
    std::atomic_bool blend_cancel = false;
    write_texture_export(blend_export, blend_cancel, [](int, const std::string &) {});
    const auto preview = composite_image(set);
    require(preview.save(directory + QString("/compare-ramp-%1.png").arg(setting + 1)),
            "Compare render");
    view->setPixmap(
        QPixmap::fromImage(preview.scaled(640, 640, Qt::KeepAspectRatio, Qt::FastTransformation)));
    workspace.show_texture_set(&set, &settings);
    workspace.set_status(QString("Compare Mask • offset %1 • width %2 • editable curve")
                             .arg(compare.compare_offset)
                             .arg(compare.compare_width));
    events(100);
    require(workspace.grab().save(directory + QString("/compare-ramp-%1-ui.png").arg(setting + 1)),
            "Compare UI screenshot");
    if (setting == 0)
      require(workspace.findChild<QWidget *>("assetShelf")
                  ->grab()
                  .save(directory + "/texture-import-assets.png"),
              "Assets screenshot");
  }
  require(QImage(directory + "/compare_export_1.png") !=
              QImage(directory + "/compare_export_2.png"),
          "Compare ramp must change actual exported pixels");
  ExportRequest request;
  request.directory = directory.toStdString();
  request.mesh = "proof";
  apply_export_preset(request, material_id_preset());
  request.outputs.front().format = ExportFormat::Jpeg8;
  request.padding = ExportPadding::None;
  ExportTile tile;
  tile.set = set;
  tile.resolution = 128;
  tile.output_resolution = 256;
  tile.normal.resize(128 * 128 * 4);
  for (int i = 0; i < 128 * 128; ++i) {
    tile.normal[4 * i] = tile.normal[4 * i + 1] = .5;
    tile.normal[4 * i + 2] = tile.normal[4 * i + 3] = 1;
  }
  request.tiles = {tile};
  std::atomic_bool cancel = false;
  write_texture_export(request, cancel, [](int, const std::string &) {});
  const auto files = export_file_list(request);
  require(files.size() == 1 && files[0].name.ends_with(".png") && files[0].channels == 3,
          "ID must be lossless RGB PNG");
  QImage id(directory + "/" + QString::fromStdString(files[0].name));
  require(!id.isNull() && id.width() == 256, "ID PNG export");
  std::set<QRgb> codes;
  for (int y = 0; y < id.height(); ++y)
    for (int x = 0; x < id.width(); ++x)
      codes.insert(id.pixel(x, y));
  const auto &a = *TextureDocument::layer(set, lower).material_id, &b = *top.material_id;
  const std::set<QRgb> expected{qRgb(a[0], a[1], a[2]), qRgb(b[0], b[1], b[2])};
  require(codes == expected, "ID export contains blended or missing colors");
  require(id.save(directory + "/material-id.png"), "ID proof copy");
  // Exercise the native-material export packet path without an OpenGL context.
  auto captured = std::make_shared<ExportCapturedPlanes>();
  captured->width = captured->height = 128;
  for (auto &plane : captured->channels)
    plane = std::make_shared<const std::vector<float>>(128 * 128 * 4, 1);
  captured->mixed_ao = captured->channels[5];
  for (const auto &layer : set.layers) {
    auto coverage = std::make_shared<std::vector<float>>(128 * 128 * 4);
    for (int y = 0; y < 128; ++y)
      for (int x = 0; x < 128; ++x)
        (*coverage)[(y * 128 + x) * 4] = layer.kind == LayerKind::Fill ?
                                             TextureDocument::mask_value(set, layer.id, x, y) :
                                             0;
    captured->material_id_coverage[layer.id] = coverage;
  }
  request.tiles[0].captured = captured;
  request.tiles[0].output_resolution = 128;
  request.outputs[0].name = "material-id-captured";
  write_texture_export(request, cancel, [](int, const std::string &) {});
  QImage captured_png(directory + "/material_id_captured.png");
  require(!captured_png.isNull(), "captured ID PNG");
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x) {
      const auto expected = TextureDocument::material_id_pixel(set, x, 127 - y);
      require(captured_png.pixel(x, y) == qRgb(qRound(expected[0] * 255),
                                               qRound(expected[1] * 255),
                                               qRound(expected[2] * 255)),
              "captured ID export differs from CPU ownership");
    }
  auto preset = material_id_preset();
  require(preset.outputs.front().source == ExportMapSource::MaterialId, "export preset");
  QFile report(directory + "/features.json");
  require(report.open(QIODevice::WriteOnly), "report");
  report.write(
      QJsonDocument(QJsonObject{{"formats", formats},
                                {"library_survives_source_deletion", true},
                                {"assets_drop", true},
                                {"channel_slots", 5},
                                {"mask_fill", true},
                                {"compare_settings", 2},
                                {"material_id_distinct_colors", int(codes.size())},
                                {"material_id_output", QString::fromStdString(files[0].name)},
                                {"gpu_runtime_verified", false}})
          .toJson());
  workspace.close();
  stop_asset_thumbnail_jobs();
  return 0;
}
}  // namespace bts
