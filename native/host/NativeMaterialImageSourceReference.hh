#pragma once
namespace blender {struct Main;struct Image;}
namespace bts {
inline constexpr const char *native_missing_image_reference_property="_BTS_missing_image_source_v1";
// Private archive copy only. Never modify the source Image or invent bytes.
void retain_missing_native_image_reference(const blender::Main &,const blender::Image &,blender::Main &,blender::Image &);
// Detached hydrate Main only, before source finishing/decoding. No file I/O.
void restore_missing_native_image_references(blender::Main &);
}
