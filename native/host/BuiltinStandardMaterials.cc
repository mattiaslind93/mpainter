#include "BuiltinStandardMaterials.hh"
#include "NativeMaterialAsset.hh"
#include "NativeMaterialImageDependencies.hh"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <set>
#include "FillTexturePool.hh"
#include "SmartMaterialPreset.hh"
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QResource>
#include <QBuffer>
#include "BlendMaterialBindings.hh"
#include "MaterialValuesJson.hh"
#include <array>
#include <stdexcept>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace bts {
namespace {
std::shared_ptr<FillTexture> decode_png_fill_texture(const StudioAsset &resource, bool raw) {
 QImage img = QImage::fromData(resource.bytes, "PNG");
 if (img.isNull()) return nullptr;
 const int w = img.width(), h = img.height();
 if (w <= 0 || h <= 0) return nullptr;
 std::vector<FillTexture::Pixel> pixels(std::size_t(w) * h);
 // Data textures can contain vectors (Normal), not only scalar channels.
 // Preserve their components and precision. Bundled color PNGs contain
 // scene-linear ACEScg values, as recorded by the channel-bake manifest.
 const auto rgba=img.convertToFormat(QImage::Format_RGBA64);
 for(int y=0;y<h;++y){
  const auto *row=reinterpret_cast<const QRgba64 *>(rgba.constScanLine(h-1-y));
  for(int x=0;x<w;++x)pixels[std::size_t(y)*w+x]={row[x].red()/65535.f,row[x].green()/65535.f,row[x].blue()/65535.f,row[x].alpha()/65535.f};
 }
 auto tex = std::make_shared<FillTexture>();
 tex->asset = resource.id.toString(QUuid::WithoutBraces).toStdString();
 tex->name = resource.name.toStdString();
 tex->hash = resource.hash().toStdString();
 tex->raw = raw;
 if (raw) tex->input_space = "Non-Color/raw";
 if (!raw) {
  tex->source_space = "ACEScg";
  tex->input_space = "ACEScg";
 }
 tex->width = w;
 tex->height = h;
 tex->pixels = intern_fill_texture_pixels(*tex, std::make_shared<const std::vector<FillTexture::Pixel>>(std::move(pixels)));
 return tex;
}

}
const std::vector<StudioAsset> &builtin_standard_microtexture_assets(){static const std::vector<StudioAsset> empty;return empty;}
const std::vector<StudioAsset> &builtin_standard_material_assets(){static const std::vector<StudioAsset> empty;return empty;}
const std::vector<StudioAsset> &builtin_standard_smart_material_assets(){static const std::vector<StudioAsset> empty;return empty;}
void resolve_user_material_textures(std::vector<StudioAsset> &assets) {
 for (auto &asset : assets) {
  if (!asset.material || !asset.fill_preset || !asset.search_terms.contains("blender-images")) continue;
  for (int channel : {0, 1, 2, 3, 4, 5}) {
   auto &tex = fill_channel_texture(*asset.fill_preset, channel);
   if (!tex || tex->pixels) continue;
   const auto found = std::find_if(assets.begin(), assets.end(), [&](const StudioAsset &candidate) {
    return candidate.usages.contains("texture") &&
           candidate.id.toString(QUuid::WithoutBraces).toStdString() == tex->asset;
   });
   if (found == assets.end()) continue;
   if (auto decoded = decode_png_fill_texture(*found, channel != 0)) {
    FillTexture updated = *tex;
    updated.pixels = decoded->pixels;
    updated.width = decoded->width;
    updated.height = decoded->height;
    if (channel == 0) {
     updated.source_space = "ACEScg";
     updated.input_space = "ACEScg";
    }
    tex = std::make_shared<const FillTexture>(std::move(updated));
   }
  }
 }
}

FolderMaterialBundle build_folder_material_bundle(const TextureSet &folder_set, const QString &name) {
 const QString trimmed = name.trimmed();
 if (trimmed.isEmpty()) throw std::invalid_argument("Material name is required");
 TextureSet eval = folder_set;
 constexpr int kRes = 128;
 eval.texture_resolution = kRes;
 eval.texture_height = kRes;
 eval.tile_resolutions.clear();
 const int tile = eval.tiles.empty() ? 1001 : *eval.tiles.begin();
 if (!eval.projection_geometry || eval.projection_geometry->resolution != kRes || eval.projection_geometry->height != kRes) {
  auto geom = std::make_shared<FillSurfaceAtlas>();
  geom->resolution = kRes;
  geom->height = kRes;
  geom->extent = 2.0f;
  auto &pts = geom->tiles[tile];
  pts.resize(std::size_t(kRes) * kRes);
  for (int y = 0; y < kRes; ++y) {
   for (int x = 0; x < kRes; ++x) {
    const float u = (float(x) + 0.5f) / float(kRes);
    const float v = (float(y) + 0.5f) / float(kRes);
    auto &pt = pts[std::size_t(y) * kRes + x];
    pt.valid = true;
    pt.position = {u * 2.0f - 1.0f, v * 2.0f - 1.0f, 0.25f};
    pt.normal = {0.0f, 0.0f, 1.0f};
    pt.footprint = 1.0f / float(kRes);
   }
  }
  eval.projection_geometry = std::move(geom);
 }
 // Provide fallback synthetic mesh maps if none exist so Mask Generators in the folder evaluate cleanly
 auto make_fallback_map = [&](const char *map_name, auto fn) {
  std::vector<float> values(std::size_t(kRes) * kRes * 4, 1.0f);
  for (int y = 0; y < kRes; ++y) {
   for (int x = 0; x < kRes; ++x) {
    const float u = (float(x) + 0.5f) / float(kRes);
    const float v = (float(y) + 0.5f) / float(kRes);
    const auto rgba = fn(u, v);
    const std::size_t idx = (std::size_t(y) * kRes + x) * 4;
    for (int c = 0; c < 4; ++c) values[idx + c] = rgba[c];
   }
  }
  auto px = std::make_shared<MeshMapPixels>(kRes, std::move(values), kRes);
  MeshMap m;
  m.name = map_name;
  m.tiles[tile] = {kRes, px->hash, px, kRes};
  return m;
 };
 if (!eval.mesh_maps[1]) {
  eval.mesh_maps[1] = make_fallback_map("World space normal", [](float u, float v) -> std::array<float, 4> {
   return {0.5f + 0.25f * std::cos(u * 6.2831853f), 0.5f + 0.35f * (1.0f - v), 0.85f, 1.0f};
  });
 }
 if (!eval.mesh_maps[3]) {
  eval.mesh_maps[3] = make_fallback_map("Ambient occlusion", [](float u, float v) -> std::array<float, 4> {
   const float ao = std::clamp(0.65f + 0.30f * std::sin(u * 6.2831853f + v * 6.2831853f), 0.1f, 1.0f);
   return {ao, ao, ao, 1.0f};
  });
 }
 if (!eval.mesh_maps[4]) {
  eval.mesh_maps[4] = make_fallback_map("Curvature", [](float u, float v) -> std::array<float, 4> {
   const float ridge = 0.5f + 0.38f * std::sin(u * 12.56637f) * std::cos(v * 12.56637f);
   return {ridge, ridge, ridge, 1.0f};
  });
 }
 if (!eval.mesh_maps[5]) {
  eval.mesh_maps[5] = make_fallback_map("Position", [](float u, float v) -> std::array<float, 4> {
   return {u, 1.0f - v, 0.5f, 1.0f};
  });
 }
 if (!eval.mesh_maps[6]) {
  eval.mesh_maps[6] = make_fallback_map("Thickness", [](float, float v) -> std::array<float, 4> {
   const float th = std::clamp(0.35f + 0.55f * v, 0.05f, 1.0f);
   return {th, th, th, 1.0f};
  });
 }

 QImage color_img(kRes, kRes, QImage::Format_RGBA8888);
 QImage metal_img(kRes, kRes, QImage::Format_Grayscale8);
 QImage rough_img(kRes, kRes, QImage::Format_Grayscale8);
 QImage height_img(kRes, kRes, QImage::Format_Grayscale8);

 double sum_metal = 0.0, sum_rough = 0.0;
 float min_metal = 1.0f, max_metal = 0.0f;
 for (int y = 0; y < kRes; ++y) {
  uchar *c_row = color_img.scanLine(kRes - 1 - y);
  uchar *m_row = metal_img.scanLine(kRes - 1 - y);
  uchar *r_row = rough_img.scanLine(kRes - 1 - y);
  uchar *h_row = height_img.scanLine(kRes - 1 - y);
  for (int x = 0; x < kRes; ++x) {
   const auto mat = TextureDocument::composite(eval, x, y, tile).material;
   c_row[x * 4 + 0] = uchar(std::clamp(int(std::lround(std::clamp(mat.base_color[0], 0.0f, 1.0f) * 255.0f)), 0, 255));
   c_row[x * 4 + 1] = uchar(std::clamp(int(std::lround(std::clamp(mat.base_color[1], 0.0f, 1.0f) * 255.0f)), 0, 255));
   c_row[x * 4 + 2] = uchar(std::clamp(int(std::lround(std::clamp(mat.base_color[2], 0.0f, 1.0f) * 255.0f)), 0, 255));
   c_row[x * 4 + 3] = 255;
   const float m = std::clamp(mat.metallic, 0.0f, 1.0f);
   const float r = std::clamp(mat.roughness, 0.0f, 1.0f);
   const float b = std::clamp(mat.bump, 0.0f, 1.0f);
   m_row[x] = uchar(std::clamp(int(std::lround(m * 255.0f)), 0, 255));
   r_row[x] = uchar(std::clamp(int(std::lround(r * 255.0f)), 0, 255));
   h_row[x] = uchar(std::clamp(int(std::lround(b * 255.0f)), 0, 255));
   sum_metal += m;
   sum_rough += r;
   min_metal = std::min(min_metal, m);
   max_metal = std::max(max_metal, m);
  }
 }

 QString slug;
 for (QChar ch : trimmed.toLower()) {
  if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) slug.append(ch);
  else if (slug.isEmpty() || !slug.endsWith('-')) slug.append('-');
 }
 while (slug.endsWith('-')) slug.chop(1);
 if (slug.isEmpty()) slug = "folder-material";

 FolderMaterialBundle bundle;
 auto make_tex_asset = [&](const QImage &img, const QString &suffix, bool raw) -> std::shared_ptr<FillTexture> {
  QByteArray bytes;
  QBuffer buf(&bytes);
  buf.open(QIODevice::WriteOnly);
  img.save(&buf, "PNG");
  StudioAsset res;
  res.id = QUuid::createUuid();
  res.name = trimmed + " " + suffix;
  res.filename = slug + "-" + suffix.toLower().replace(' ', '-') + ".png";
  res.scope = AssetScope::User;
  res.usages = {"texture"};
  res.search_terms = {"internal:folder-material-texture", "user-folder-material"};
  res.bytes = bytes;
  auto tex = decode_png_fill_texture(res, raw);
  bundle.textures.push_back(std::move(res));
  return tex;
 };

 FillProjection mapping;
 mapping.mode = 0; // UV default, switchable to Tri-planar (1)
 mapping.hardness = 0.5f;
 mapping.scale = {1.0, 1.0};
 mapping.scale3 = {1.0, 1.0, 1.0};
 mapping.alpha_mask.fill(false);
 mapping.texture = make_tex_asset(color_img, "Base Color", false);
 if (max_metal - min_metal > 1.0f / 255.0f) {
  fill_channel_texture(mapping, 1) = make_tex_asset(metal_img, "Metallic", true);
 }
 fill_channel_texture(mapping, 2) = make_tex_asset(rough_img, "Roughness", true);
 fill_channel_texture(mapping, 4) = make_tex_asset(height_img, "Height", true);

 MaterialValues values = new_base_values();
 values.base_color = {1.0f, 1.0f, 1.0f, 1.0f};
 values.metallic = float(sum_metal / double(kRes * kRes));
 values.roughness = float(sum_rough / double(kRes * kRes));
 values.bump = 0.5f;
 values.ambient_occlusion = 1.0f;

 StudioAsset mat;
 mat.id = QUuid::createUuid();
 mat.name = trimmed;
 mat.filename = slug + ".btsuniform";
 mat.scope = AssetScope::User;
 mat.usages = {"material"};
 mat.search_terms = {"blender-images", "user-folder-material", "pbr"};
 mat.material = values;
 mat.fill_preset = mapping;
 mat.bytes = QJsonDocument(QJsonObject{
  {"material", material_values_json(*mat.material)},
  {"images", blend_material_bindings_json(*mat.fill_preset)}
 }).toJson(QJsonDocument::Compact);
 bundle.material = std::move(mat);
 return bundle;
}

} // namespace bts
