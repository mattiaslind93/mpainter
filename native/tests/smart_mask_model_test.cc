#include "AssetCatalog.hh"
#include "AssetCategories.hh"
#include "BuiltinSmartMasks.hh"
#include "MaterialSpherePreview.hh"
#include "DocumentEditCommit.hh"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QPainter>
#include <QJsonDocument>
#include <QFile>
#include <cassert>
#include <cstdio>
#include <set>
using namespace bts;
int main(int argc, char **argv)
{
  QGuiApplication app(argc, argv);
  QTemporaryDir directory;
  AssetCatalog catalog(directory.path());
  const auto &assets = builtin_smart_mask_assets();
  assert(assets.size() == 10);
  std::set<QUuid> identities;
  QImage sheet(5 * 224, 2 * 250, QImage::Format_RGB32);
  sheet.fill(QColor(theme::Canvas));
  QPainter painter(&sheet);
  painter.setPen(QColor(theme::Text));
  const auto surface = detail::shaderball_uv_surface(128);
  MaskCompareInputs context;
  context.projection = &surface.atlas;
  QJsonArray receipt;
  int index = 0;
  for (const auto &asset : assets) {
    assert(asset_in_category(asset, "Mask generators"));
    assert(asset.smart_mask && identities.insert(asset.id).second);
    assert(asset.filename.endsWith(".btsmask"));
    const auto decoded = read_mask_preset(asset.bytes);
    assert(write_mask_preset(decoded) == write_mask_preset(*asset.smart_mask));
    TextureSet set;
    set.texture_resolution = set.texture_height = 128;
    set.mesh_maps = surface.maps;
    set.projection_geometry = std::make_shared<FillSurfaceAtlas>(surface.atlas);
    for (bool folder : {false, true}) {
      if (folder) TextureDocument::add_folder(set); else TextureDocument::add_fill(set);
      const auto id = set.selected_layer;
      TextureDocument::apply_mask_preset(set, id, decoded, true);
      const auto &layer = TextureDocument::layer(set, id);
      assert(layer.mask && layer.kind == (folder ? LayerKind::Folder : LayerKind::Fill));
      assert(layer.mask->effects.front().generator == decoded.effects.front().generator);
      const auto applied = write_mask_preset(*layer.mask);
      const auto before = set;
      TextureDocument::layer(set, id).mask->effects.front().invert = !decoded.effects.front().invert;
      assert(write_mask_preset(*TextureDocument::layer(set, id).mask) != applied);
      set = before;
      assert(write_mask_preset(*TextureDocument::layer(set, id).mask) == applied);
    }
    float low = 1, high = 0;
    double changed = 0;
    auto inverted = decoded;
    inverted.effects.front().invert = !inverted.effects.front().invert;
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
      const float value = decoded.value(x, y, 1001, 128, 128, nullptr, &surface.maps, &context);
      const float other = inverted.value(x, y, 1001, 128, 128, nullptr, &surface.maps, &context);
      assert(std::isfinite(value) && value >= 0 && value <= 1);
      low = std::min(low, value); high = std::max(high, value);
      changed += std::abs(other - value);
    }
    assert(changed > 1);
    assert(high > .1f);
    if (asset.name != "Curvature Ridge") assert(high - low > .05f);
    const auto preview = smart_mask_sphere_preview(decoded, 208);
    assert(!preview.isNull());
    const int x = (index % 5) * 224 + 8, y = (index / 5) * 250;
    painter.drawImage(x, y, preview);
    painter.drawText(QRect(x, y + 210, 208, 32), Qt::AlignCenter, asset.name);
    receipt.append(QJsonObject{{"name", asset.name}, {"minimum", low}, {"maximum", high}, {"invert_difference", changed}, {"fill_and_folder", true}});
    ++index;
  }
  painter.end();
  assert(builtin_smart_mask_rcc_decodes() == 0);
  if (argc > 1) {
    const QString output = QString::fromLocal8Bit(argv[1]);
    QDir().mkpath(output);
    assert(sheet.save(output + "/mask-generators.png"));
    QFile file(output + "/mask-generators.json");
    assert(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(receipt).toJson());
  }
  std::puts("PASS: 10 shelf generators, sphere evaluation, inversion, Fill/Folder application, codec roundtrip, no bundled archives");
}
