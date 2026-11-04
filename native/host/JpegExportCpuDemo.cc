#include "JpegExportCpuDemo.hh"
#include "TextureExport.hh"
#include "ExportConfiguration.hh"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
namespace bts {
QJsonObject run_jpeg_export_cpu_demo(const QString &directory){
 const auto require=[](bool ok,const char *message){if(!ok)throw std::runtime_error(message);};
 // Keep "Normal" in an artist-authored name as a permanent filename regression.
 TextureSet set;set.name="Normal_JpegSurface";set.texture_resolution=128;set.base_values.base_color={.18,.18,.18,1};set.base_values.roughness=.37;set.base_values.metallic=.7;auto &layer=set.layers.front();layer.kind=LayerKind::Fill;layer.values=set.base_values;
 ExportTile tile{set,1001,128};tile.normal.resize(128*128*4);for(std::size_t p=0;p<tile.normal.size();p+=4){tile.normal[p]=tile.normal[p+1]=.5;tile.normal[p+2]=tile.normal[p+3]=1;}
 tile.coverage.resize(128*128);for(int y=32;y<96;++y)for(int x=32;x<96;++x)tile.coverage[y*128+x]=1;
 ExportRequest request;request.directory=QDir(directory).absolutePath().toStdString();QDir().mkpath(QString::fromStdString(request.directory));request.tiles={tile};request.format=ExportFormat::Jpeg8;request.padding=ExportPadding::Transparent;request.padding_width=8;apply_export_preset(request,usd_metal_rough_preset());
 const auto files=export_file_list(request);require(files.size()==4,"USD preset did not select color, metallic, roughness and occlusion");std::atomic_bool cancel=false;write_texture_export(request,cancel,[](int,const std::string &){});QJsonArray outputs;
 for(const auto &file:files){const auto path=QDir(directory).absoluteFilePath(QString::fromStdString(file.name));require(file.channel!=3&&file.channel!=6,"USD preset exported a normal map");QFile bytes(path);require(bytes.open(QIODevice::ReadOnly),"Cannot read JPEG export");const auto header=bytes.read(8);require(header.startsWith(QByteArray::fromHex("ffd8")),"USD export file codec does not match extension");
  using Image=std::unique_ptr<blender::ImBuf,decltype(&blender::IMB_freeImBuf)>;Image image(blender::IMB_load_image_from_filepath(path.toUtf8().constData(),blender::ImBufFlags::ByteData|blender::ImBufFlags::NoColorspaceConvert),blender::IMB_freeImBuf);require(bool(image)&&image->x==128&&image->y==128&&image->byte_data(),"Cannot decode native JPEG/PNG export");const auto *pixel=image->byte_data()+(64*128+64)*4;
  if(file.channel==0){for(int c=0;c<3;++c)require(std::abs(int(pixel[c])-118)<=1,"JPEG color has a missing or repeated sRGB conversion");require(std::abs(int(image->byte_data()[0])-118)<=1,"JPEG transparency must flatten onto the channel default");}
  else if(file.channel==1)require(std::abs(int(pixel[0])-178)<=1,"JPEG Metallic is not raw data");
  else if(file.channel==2)require(std::abs(int(pixel[0])-94)<=1,"JPEG Roughness is not raw data");
  else if(file.channel==7)require(pixel[0]==255,"Missing AO must export neutral white");
  else throw std::runtime_error("Unexpected USD export output");
  outputs.append(QJsonObject{{"name",QString::fromStdString(file.name)},{"channels",file.channels},{"center",QJsonArray{pixel[0],pixel[1],pixel[2]}}});
 }
 return {{"files",outputs},{"jpeg_quality",100},{"jpeg_color_srgb_once",true},{"scalars_raw",true},{"normal_map_exported",false},{"missing_ao_neutral",true},{"jpeg_transparency_default_background",true}};
}
}
