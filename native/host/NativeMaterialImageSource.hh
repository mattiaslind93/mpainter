#pragma once
#include <QString>
#include <vector>
namespace blender {struct Main;struct Image;struct PackedFile;}
namespace bts {
// Exact source selection, independent of process Main/image caches. No I/O.
struct NativeMaterialEncodedImageSource {QString path,filename;const blender::PackedFile *packed=nullptr;int tile=0;};
std::vector<int> native_material_tiled_image_numbers(const blender::Image &);
QString native_material_tiled_image_pattern(const blender::Main &,const blender::Image &);
NativeMaterialEncodedImageSource native_material_encoded_image_source(const blender::Main &,const blender::Image &,int tile=0);
}
