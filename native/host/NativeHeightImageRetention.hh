#pragma once
#include "NativeMaterialBundle.hh"
#include <span>
namespace bts {
enum class NativeHeightImageRetention {PackedArchive,VerifiedBundleReference};
// A list entry alone is not retained image data: damaged/empty PackedFile
// records must not authorize an original filepath or decoded-cache fallback.
template<class Parts> bool native_height_has_packed_image_bytes(const Parts &parts){
 for(const auto &part:parts)if(part.packedfile&&part.packedfile->size>0&&part.packedfile->data)return true;
 return false;
}
// The selected image must survive removal of every original source path.
// Packed bytes are inside the SHA-identified native archive. External bytes
// require the exact image-name binding AND verified resource hash; an existing
// Image.filepath or decoded runtime cache is never evidence of retention.
template<class VerifyHash> NativeHeightImageRetention require_native_height_image_retention(
 const QString &image,bool archive_packed,std::span<const NativeMaterialImageReference> references,VerifyHash &&verified){
 if(image.isEmpty())throw std::invalid_argument("Raw Height image has no immutable image identity");
 if(archive_packed)return NativeHeightImageRetention::PackedArchive;
 const NativeMaterialImageReference *selected=nullptr;
 for(const auto &reference:references)if(reference.image==image){
  if(selected)throw std::invalid_argument("Raw Height image has ambiguous bundle bindings");
  selected=&reference;
 }
 if(!selected)throw std::invalid_argument("Raw Height image is not retained by packed archive bytes or a verified bundle reference; reimport the material with its image available");
 selected->validate();
 if(!verified(selected->sha256))throw std::invalid_argument("Raw Height image bundle resource is missing or its checksum changed");
 return NativeHeightImageRetention::VerifiedBundleReference;
}
}
