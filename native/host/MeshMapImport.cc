#include "TextureExtent.hh"
#include "TextureMemory.hh"
#include "MeshMapImport.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include <QFile>
#include <QFileInfo>
namespace bts {
namespace {
MeshMapPlane load_plane(const std::string &path){
  using Buffer=std::unique_ptr<blender::ImBuf,decltype(&blender::IMB_freeImBuf)>;
  const auto flags=blender::ImBufFlags::FloatData|blender::ImBufFlags::NoColorspaceConvert|blender::ImBufFlags::AlphaChannelPacked;
    const auto file_path=QString::fromStdString(path);QFileInfo info(file_path);if(!info.isAbsolute()||!info.isFile()||info.size()<1)throw std::invalid_argument("Utility map must be a readable image: "+path);
    const auto encoded_path=file_path.toUtf8();
    Buffer header(blender::IMB_load_image_from_filepath(encoded_path.constData(),flags|blender::ImBufFlags::Test),blender::IMB_freeImBuf);
    if(!header)throw std::invalid_argument("Cannot decode utility-map image header: "+path);
    const int width=header->x,height=header->y;
    require_texture_resolution(width,"Utility-map width",32,true);require_texture_resolution(height,"Utility-map height",32,true);
    // Completed maps can live in evictable disk snapshots. Admit only the new
    // native decode and RGBA staging buffers against currently available RAM;
    // an in-RAM cache fallback is already reflected in that fresh query.
    header.reset();
    Buffer image(blender::IMB_load_image_from_filepath(encoded_path.constData(),flags),blender::IMB_freeImBuf);
    if(!image||image->x!=width||image->y!=height||(!image->float_data()&&!image->byte_data()))throw std::invalid_argument("Cannot decode utility-map image pixels: "+path);
    const int channels=image->channels?image->channels:4;if(channels<1||channels>4)throw std::invalid_argument("Unsupported utility-map channels: "+path);
    std::vector<float> rgba(std::size_t(width)*height*4);
    for(std::size_t i=0;i<std::size_t(width)*height;++i){for(int c=0;c<3;++c)rgba[i*4+c]=image->float_data()?image->float_data()[i*channels+(channels<3?0:c)]:image->byte_data()[i*4+c]/255.f;rgba[i*4+3]=1;}
  auto pixels=std::make_shared<MeshMapPixels>(width,std::move(rgba),height);return {width,pixels->hash,std::move(pixels),height};
}
}
MeshMapPlane load_mesh_map_image(const std::string &path){return load_plane(path);}
MeshMapImports load_utility_maps(const std::vector<std::string> &paths){
  auto descriptors=resource_product(paths.size(),sizeof(ImportedMeshMap)+sizeof(std::string));
  for(const auto &path:paths)descriptors=resource_sum(descriptors,resource_product(path.size(),2));
  MeshMapImports result;
  for(const auto &path:paths){const auto name=mesh_map_import_name(path);if(!name){result.ignored.push_back("Ignored utility map with unsupported naming: "+path);continue;}result.maps.push_back({*name,load_plane(path)});}return result;
}
}
