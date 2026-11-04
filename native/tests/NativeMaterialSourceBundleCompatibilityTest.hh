#pragma once
// Pure CPU transport checks over the actual archived Blender payload. The
// initialized availability fixture separately verifies real Image provenance.
#include "NativeMaterialBundle.hh"

namespace bts {
inline QJsonObject verify_native_material_source_bundle_compatibility(const QByteArray &archive_bytes)
{
 const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
 const auto unchanged=archive_bytes;
 const auto actual=read_native_material_bundle(archive_bytes);
 require(actual.external&&!actual.blend.isEmpty(),"Bundle compatibility requires an actual source archive");
 const auto raw=read_native_material_bundle(actual.blend);
 require(!raw.external&&raw.blend==actual.blend&&raw.images.empty()&&raw.preview_png.isEmpty()&&!native_material_bundle_provenance(actual.blend),"Legacy raw transport changed actual source payload or invented metadata");
 const auto legacy=native_material_bundle_bytes(actual.blend,actual.images);
 const auto preview_a=QByteArray("CPU preview transport A"),preview_b=QByteArray("CPU preview transport B");
 const auto with_preview=native_material_bundle_bytes(actual.blend,actual.images,{},preview_a);
 const auto legacy_read=read_native_material_bundle(with_preview);
 const auto rewritten=native_material_bundle_bytes(legacy_read.blend,legacy_read.images,native_material_bundle_provenance(with_preview),preview_b);
 require(native_material_bundle_preview(with_preview)==preview_a&&native_material_bundle_preview(rewritten)==preview_b&&native_material_source_hash(legacy)==native_material_source_hash(with_preview)&&native_material_source_hash(with_preview)==native_material_source_hash(rewritten),"Legacy preview rewrite changed source identity or lost preview bytes");
 require(!native_material_bundle_provenance(legacy)&&!native_material_bundle_provenance(with_preview)&&!native_material_bundle_provenance(rewritten),"Legacy v1 transport invented variant lineage");
 // Synthetic valid lineage exercises only the existing v2 transport contract;
 // it does not claim an image replacement or modify the actual Blender payload.
 NativeImageBinding binding{{},101,202,"Color","Color1","ShaderNodeMixRGB","Compatibility Image",
  {{"image_source",1},{"image_type",0},{"alpha_mode",0},{"high_precision",true},{"multiview",false},{"source_space","sRGB"},{"interpolation",1},{"projection",0},{"projection_blend",0.},{"extension",0}}};
 NativeImageVariantProvenance lineage{QUuid("{1749fbb2-495d-4095-a9d3-3780ae722023}"),QString::fromLatin1(QCryptographicHash::hash(archive_bytes,QCryptographicHash::Sha256).toHex()),{{binding,QString(64,'b'),"sRGB","original-declared"}}};
 const auto variant=native_material_bundle_bytes(actual.blend,actual.images,lineage);
 const auto variant_preview=native_material_bundle_bytes(actual.blend,actual.images,lineage,preview_a);
 const auto variant_read=read_native_material_bundle(variant_preview);
 const auto variant_rewritten=native_material_bundle_bytes(variant_read.blend,variant_read.images,native_material_bundle_provenance(variant_preview),preview_b);
 require(native_material_bundle_preview(variant_preview)==preview_a&&native_material_bundle_preview(variant_rewritten)==preview_b&&native_material_source_hash(variant)==native_material_source_hash(variant_preview)&&native_material_source_hash(variant_preview)==native_material_source_hash(variant_rewritten),"Variant preview rewrite changed source identity or lost preview bytes");
 const auto expected_lineage=native_image_variant_provenance_json(lineage);
 for(const auto &bytes:{variant,variant_preview,variant_rewritten}){
  const auto decoded=native_material_bundle_provenance(bytes);
  require(decoded&&native_image_variant_provenance_json(*decoded)==expected_lineage,"Variant v2 rewrite lost exact Reset lineage/operation metadata");
 }
 int transports=0;
 for(const auto &bytes:{legacy,with_preview,rewritten,variant,variant_preview,variant_rewritten}){
  const auto decoded=read_native_material_bundle(bytes);
  require(decoded.external&&decoded.blend==actual.blend&&decoded.images.size()==actual.images.size(),"Bundle rewrite changed actual Blender payload or image reference count");
  for(std::size_t i=0;i<actual.images.size();++i)require(decoded.images[i].image==actual.images[i].image&&decoded.images[i].sha256==actual.images[i].sha256&&decoded.images[i].filename==actual.images[i].filename,"Bundle rewrite changed encoded image source references");
  ++transports;
 }
 require(archive_bytes==unchanged,"Pure compatibility helper modified the actual input archive");
 return {{"actual_archive_payload_used",true},{"legacy_raw_payload_exact",true},{"rewritten_transports",transports},{"inner_blend_payload_exact",true},{"image_references_exact",true},{"legacy_v1_has_no_variant_lineage",true},{"variant_v2_lineage_exact",true},{"preview_rewrite_source_hash_unchanged",true},{"preview_bytes_replaced",true},{"input_archive_bytes_unchanged",true},{"ID_or_GPU_access",false}};
}
}
