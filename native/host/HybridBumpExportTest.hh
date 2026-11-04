#pragma once
#include "TextureExport.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include <QDir>
#include <QFile>
#include <QJsonObject>
namespace bts {
inline QJsonObject verify_hybrid_bump_export(const QString &directory){
 const auto require=[](bool ok,const char *why){if(!ok)throw std::runtime_error(why);};
 constexpr int w=128,h=256;const auto count=std::size_t(w)*h;
 const auto plane=[&](std::array<float,3> v){auto p=std::make_shared<std::vector<float>>(count*4);for(std::size_t i=0;i<count;++i){for(int c=0;c<3;++c)(*p)[i*4+c]=v[c];(*p)[i*4+3]=1;}return p;};
 auto normals=plane({.5f,.5f,1});for(int y=0;y<h;++y)for(int x=0;x<w;++x){const double p=.3*std::sin(2*std::numbers::pi*x/w),q=.2*std::cos(4*std::numbers::pi*y/h),len=std::sqrt(1+p*p+q*q);const auto i=(y*w+x)*4;(*normals)[i]=float(.5-.5*p/len);(*normals)[i+1]=float(.5-.5*q/len);(*normals)[i+2]=float(.5+.5/len);}
 const auto original=*normals;const auto height=hybrid_normal_to_bump(*normals,w,h);
 auto packet=std::make_shared<ExportCapturedPlanes>();packet->width=w;packet->height=h;for(int c=0;c<6;++c)packet->channels[c]=plane({.55f,.55f,.55f});packet->channels[3]=plane({.5,.5,1});packet->mixed_ao=plane({1,1,1});packet->normal_perturbation=normals;
 ExportTile tile;tile.set.name="Hybrid";tile.resolution=w;tile.height=h;tile.captured=packet;
 ExportRequest request;request.format=ExportFormat::Exr32;request.tiles={tile};request.selected_channels=std::array<bool,9>{false,false,false,false,true,false,false,false,false};request.packed=PackedOutput{"$textureSet_packed",{PackedSource::Bump,PackedSource::Bump,PackedSource::Bump}};
 std::atomic_bool cancel=false;int checked=0;
 for(int mode=0;mode<3;++mode){
  request.directory=QDir(directory).filePath(QString("hybrid-bump-%1").arg(mode)).toStdString();QDir().mkpath(QString::fromStdString(request.directory));
  if(mode==1){auto weights=std::make_shared<std::vector<float>>(count,.4f);request.tiles[0].normal_contributions={{normals,weights,.25f,false},{normals,{},.3f,false}};}
  if(mode==2)request.tiles[0].normal_contributions={{normals,{},1,true}};
  write_texture_export(request,cancel,[](int,const std::string &){});
  for(const auto &file:export_file_list(request)){
   require(file.channel==4||file.channel==9,"Unexpected hybrid output");QFile input(QDir(QString::fromStdString(request.directory)).filePath(QString::fromStdString(file.name)));require(input.open(QIODevice::ReadOnly),"Hybrid export missing");const auto bytes=input.readAll();
   using Image=std::unique_ptr<blender::ImBuf,decltype(&blender::IMB_freeImBuf)>;Image image(blender::IMB_load_image_from_memory(reinterpret_cast<const unsigned char *>(bytes.constData()),bytes.size(),blender::ImBufFlags::NoColorspaceConvert|blender::ImBufFlags::AlphaChannelPacked,"hybrid.exr"),blender::IMB_freeImBuf);
   require(image&&image->float_data()&&image->x==w&&image->y==h,"Hybrid export dimensions/format changed");
   for(std::size_t i=0;i<count;++i){const float expected=(mode==2?.5f:.55f)+(height[i]-.5f)*(mode==1?.4f:1.f);for(int c=0;c<3;++c)require(std::abs(image->float_data()[i*4+c]-expected)<2e-6,"Hybrid export dropped/doubled Height or normal contribution");}
   ++checked;
  }
 }
 require(*normals==original,"Export changed imported normal pixels");
 return {{"complete",true},{"files_checked",checked},{"native_plus_scalar",true},{"per_source_weights",true},{"raw_surface_no_double_count",true},{"rectangular",true},{"source_unchanged",true}};
}
}
