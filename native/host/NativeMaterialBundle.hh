#pragma once
#include "ResourceAvailability.hh"
#include "TextureResource.hh"
#include "NativeMaterialImageVariantMetadata.hh"
#include <QByteArray>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
namespace bts {
enum class NativeMaterialImageSourceKind {File,Tiled};
struct NativeMaterialImageReference {
  QString image,sha256,filename;
  NativeMaterialImageSourceKind source=NativeMaterialImageSourceKind::File;
  std::vector<TextureSourceFile> tiles;
  void validate() const {

    if(image.isEmpty()||image.toUtf8().size()>255||image.contains(QChar::Null)||filename.isEmpty()||filename=="."||filename==".."||filename.contains('/')||filename.contains('\\')||filename.contains(QChar::Null))throw std::invalid_argument("Invalid native material image reference");
    if(sha256.size()!=64||!std::all_of(sha256.begin(),sha256.end(),[](QChar c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))throw std::invalid_argument("Invalid native material image hash");
    if(source==NativeMaterialImageSourceKind::File){if(!tiles.empty())throw std::invalid_argument("Single-file native reference has tiled metadata");}
    else if(source==NativeMaterialImageSourceKind::Tiled){
      validate_texture_sources(tiles);const bool udim=filename.count("<UDIM>")==1,uvtile=filename.count("<UVTILE>")==1;
      if(udim==uvtile||filename.count("<UDIM>")+filename.count("<UVTILE>")!=1)throw std::invalid_argument("Native TILED reference needs one explicit tile token");
      for(const auto &tile:tiles){if(tile.tile<1001||!tile.path.isEmpty())throw std::invalid_argument("Invalid portable native tile metadata");auto expected=filename;if(udim)expected.replace("<UDIM>",QString::number(tile.tile));else expected.replace("<UVTILE>",QString("u%1_v%2").arg((tile.tile-1001)%10+1).arg((tile.tile-1001)/10+1));if(tile.filename!=expected)throw std::invalid_argument("Native tile filename differs from its exact token identity");}
      if(texture_sources_hash(tiles)!=sha256)throw std::invalid_argument("Native tile aggregate hash mismatch");
    }else throw std::invalid_argument("Unknown native image source kind");
  }
};
struct NativeMaterialBundle {
  QByteArray blend;
  std::vector<NativeMaterialImageReference> images;
  bool external=false;
  QByteArray preview_png;
};
inline QByteArray native_material_bundle_bytes(const QByteArray &blend,const std::vector<NativeMaterialImageReference> &images,
                                              const std::optional<NativeImageVariantProvenance> &variant={},const QByteArray &preview_png={})
{
  if(blend.isEmpty())throw std::invalid_argument("Invalid native material bundle");
  std::uint64_t metadata=128;
  for(const auto &image:images){
    const auto characters=resource_sum(resource_sum(image.image.size(),image.filename.size()),image.sha256.size());
    metadata=resource_sum(metadata,resource_sum(sizeof(NativeMaterialImageReference)+256,resource_product(characters,12)));
    for(const auto &tile:image.tiles)metadata=resource_sum(metadata,resource_sum(sizeof(TextureSourceFile)+256,resource_product(resource_sum(tile.filename.size(),tile.sha256.size()),12)));
  }
  metadata=resource_sum(metadata,resource_product(preview_png.size(),4));
  if(variant)metadata=resource_sum(metadata,native_image_variant_provenance_bytes(*variant));

  const bool tiled=std::any_of(images.begin(),images.end(),[](const auto &image){return image.source==NativeMaterialImageSourceKind::Tiled;});
  QJsonArray references;QStringList names;
  for(const auto &image:images){image.validate();if(names.contains(image.image))throw std::invalid_argument("Duplicate native image name");names.append(image.image);QJsonObject row{{"image",image.image},{"sha256",image.sha256},{"filename",image.filename}};if(tiled){row["source"]=image.source==NativeMaterialImageSourceKind::Tiled?"TILED":"FILE";if(image.source==NativeMaterialImageSourceKind::Tiled)row["tiles"]=texture_sources_json(image.tiles,false);}references.append(row);}
  QJsonObject descriptor{{"version",tiled?3:variant?2:1},{"images",references}};
  if(!preview_png.isEmpty())descriptor["preview_png"]=QString::fromLatin1(preview_png.toBase64());
  if(variant)descriptor["image_variant"]=native_image_variant_provenance_json(*variant);
  const auto manifest=QJsonDocument(descriptor).toJson(QJsonDocument::Compact);
  QByteArray result("MFPNAT1\n",8);char sizes[16];qToBigEndian<quint64>(manifest.size(),sizes);qToBigEndian<quint64>(blend.size(),sizes+8);result.append(sizes,16);result.append(manifest);result.append(blend);return result;
}
// Used by manifest-only preview/lineage readers: never materialize blend bytes.
inline std::vector<NativeMaterialImageReference> native_material_bundle_image_references(const QJsonObject &data){
 if(!data["images"].isArray())throw std::invalid_argument("Invalid native image reference table");
 if(data["version"]==3){if(data.size()!=2+int(data.contains("preview_png"))+int(data.contains("image_variant")))throw std::invalid_argument("Invalid native tiled manifest");if(data.contains("image_variant")){if(!data["image_variant"].isObject())throw std::invalid_argument("Invalid native tiled variant provenance");read_native_image_variant_provenance(data["image_variant"].toObject());}}
 std::vector<NativeMaterialImageReference> result;QStringList names;
  for(const auto &value:data["images"].toArray()){if(!value.isObject())throw std::invalid_argument("Invalid native image entry");const auto entry=value.toObject();NativeMaterialImageReference image{entry["image"].toString(),entry["sha256"].toString(),entry["filename"].toString()};if(data["version"]==3){const auto kind=entry["source"].toString();if(kind=="TILED"){image.source=NativeMaterialImageSourceKind::Tiled;if(entry.size()!=5)throw std::invalid_argument("Invalid native TILED image entry");for(const auto &tile:entry["tiles"].toArray())if(!tile.isObject()||tile.toObject().size()!=4)throw std::invalid_argument("Invalid portable native tile entry");image.tiles=read_texture_sources(entry["tiles"]);}else if(kind!="FILE"||entry.size()!=4)throw std::invalid_argument("Invalid native FILE image entry");}image.validate();if(names.contains(image.image))throw std::invalid_argument("Duplicate native image name");names.append(image.image);result.push_back(std::move(image));}
 return result;
}
// Shelf workers inspect only the small manifest, never material/image resources.
inline QByteArray native_material_bundle_preview(const QByteArray &bytes)
{
  if(!bytes.startsWith(QByteArrayView("MFPNAT1\n",8)))return {};
  if(bytes.size()<24)throw std::invalid_argument("Truncated native material bundle");
  const auto size=qFromBigEndian<quint64>(bytes.constData()+8),blend=qFromBigEndian<quint64>(bytes.constData()+16);
  if(!size||!blend||size>quint64(bytes.size()-24)||blend!=quint64(bytes.size()-24)-size)throw std::invalid_argument("Invalid native material bundle lengths");

  QJsonParseError error;const auto doc=QJsonDocument::fromJson(QByteArray::fromRawData(bytes.constData()+24,qsizetype(size)),&error);const auto o=doc.object();
  if(error.error!=QJsonParseError::NoError||!doc.isObject()||(o["version"]!=1&&o["version"]!=2&&o["version"]!=3))throw std::invalid_argument("Invalid native material bundle manifest");
  if(o["version"]==3)(void)native_material_bundle_image_references(o);
  if(!o.contains("preview_png"))return {};
  if(!o["preview_png"].isString())throw std::invalid_argument("Invalid native material preview");
  const auto png=QByteArray::fromBase64(o["preview_png"].toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
  if(png.isEmpty())throw std::invalid_argument("Invalid native material preview encoding");return png;
}
// Provenance has a separate value API so the existing bundle transport layout
// does not change. The v2 reader validates it even when only images are needed.
inline std::optional<NativeImageVariantProvenance> native_material_bundle_provenance(const QByteArray &bytes)
{
  if(!bytes.startsWith(QByteArrayView("MFPNAT1\n",8)))return {};
  if(bytes.size()<24)throw std::invalid_argument("Truncated native material bundle");
  const auto size=qFromBigEndian<quint64>(bytes.constData()+8),blend=qFromBigEndian<quint64>(bytes.constData()+16);
  if(!size||!blend||size>quint64(bytes.size()-24)||blend!=quint64(bytes.size()-24)-size)throw std::invalid_argument("Invalid native material bundle lengths");

  QJsonParseError error;const auto doc=QJsonDocument::fromJson(bytes.sliced(24,qsizetype(size)),&error);const auto o=doc.object();
  if(error.error!=QJsonParseError::NoError||!doc.isObject()||(o["version"]!=1&&o["version"]!=2&&o["version"]!=3))throw std::invalid_argument("Invalid native material bundle manifest");
  if(o["version"]==1){if(o.contains("image_variant"))throw std::invalid_argument("Native image variant requires manifest version 2");return {};}
  if(o["version"]==3){(void)native_material_bundle_image_references(o);if(o.size()!=2+int(o.contains("preview_png"))+int(o.contains("image_variant"))||!o["images"].isArray())throw std::invalid_argument("Invalid native tiled manifest");if(!o.contains("image_variant"))return {};}
  else if(o.size()!=(o.contains("preview_png")?4:3)||!o["images"].isArray()||!o["image_variant"].isObject())throw std::invalid_argument("Invalid native image variant manifest");
  if(!o["image_variant"].isObject())throw std::invalid_argument("Invalid native image variant manifest");return read_native_image_variant_provenance(o["image_variant"].toObject());
}
// Source preparation needs only typed references, never another copy of blend.
inline std::vector<NativeMaterialImageReference> native_material_bundle_image_manifest(const QByteArray &bytes){
 if(!bytes.startsWith(QByteArrayView("MFPNAT1\n",8)))return {};
 (void)native_material_bundle_provenance(bytes);const auto size=qFromBigEndian<quint64>(bytes.constData()+8);QJsonParseError error;const auto document=QJsonDocument::fromJson(QByteArray::fromRawData(bytes.constData()+24,qsizetype(size)),&error);if(error.error!=QJsonParseError::NoError||!document.isObject())throw std::invalid_argument("Invalid native image manifest");return native_material_bundle_image_references(document.object());
}
inline NativeMaterialBundle read_native_material_bundle(const QByteArray &bytes)
{
  if(!bytes.startsWith(QByteArrayView("MFPNAT1\n",8)))return {bytes,{},false};
  if(bytes.size()<24)throw std::invalid_argument("Truncated native material bundle");
  const auto manifest_size=qFromBigEndian<quint64>(bytes.constData()+8),blend_size=qFromBigEndian<quint64>(bytes.constData()+16);
  if(!manifest_size||!blend_size||manifest_size>quint64(bytes.size()-24)||blend_size!=quint64(bytes.size()-24)-manifest_size)throw std::invalid_argument("Invalid native material bundle lengths");

  QJsonParseError error;const auto doc=QJsonDocument::fromJson(bytes.sliced(24,qsizetype(manifest_size)),&error);const auto data=doc.object();
  if(error.error!=QJsonParseError::NoError||!doc.isObject()||(data["version"]!=1&&data["version"]!=2&&data["version"]!=3)||!data["images"].isArray())throw std::invalid_argument("Invalid native material bundle manifest");
  if(data["version"]==1&&data.contains("image_variant"))throw std::invalid_argument("Native image variant requires bundle manifest version 2");
  if(data["version"]==2&&(data.size()!=(data.contains("preview_png")?4:3)||!data["image_variant"].isObject()))throw std::invalid_argument("Invalid native image variant bundle manifest");
  if(data["version"]==3&&data.size()!=2+int(data.contains("preview_png"))+int(data.contains("image_variant")))throw std::invalid_argument("Invalid native tiled bundle manifest");

  NativeMaterialBundle result;result.external=true;QStringList names;
  if(data["version"]==2||(data["version"]==3&&data.contains("image_variant"))){if(!data["image_variant"].isObject())throw std::invalid_argument("Invalid native tiled variant provenance");read_native_image_variant_provenance(data["image_variant"].toObject());}
  result.images=native_material_bundle_image_references(data);
  if(data.contains("preview_png")){if(!data["preview_png"].isString())throw std::invalid_argument("Invalid native material preview");result.preview_png=QByteArray::fromBase64(data["preview_png"].toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);if(result.preview_png.isEmpty())throw std::invalid_argument("Invalid native material preview encoding");}
  result.blend=bytes.sliced(24+qsizetype(manifest_size));return result;
}
// Shader identity excludes display-only previews; transport identity remains the
// exact catalog byte hash. Worker-only normalization shares old/new archives.
inline std::string native_material_source_hash(const QByteArray &bytes){const auto bundle=read_native_material_bundle(bytes);const auto source=native_material_bundle_bytes(bundle.blend,bundle.images,native_material_bundle_provenance(bytes));return QCryptographicHash::hash(source,QCryptographicHash::Sha256).toHex().toStdString();}
}
