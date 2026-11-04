#pragma once
#include "NativeMaterialRawHeightFixture.hh"
#include "NativeHeightImageReference.hh"
#include <QtEndian>
#include "BKE_image.hh"
#include "DNA_image_types.h"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include <QScopeGuard>
namespace bts::height_image_fixture {
using namespace blender;
using native_raw_height_test::require;
using native_raw_height_test::socket;
enum class Mutation {None,Alpha,ImplicitColor,ColorSpace,Generated,Udims,CustomNormal,HSV,Green,MathAfterRed,Cubic,Clip,Environment};
enum class Layout {RGB,Y,RGBAZero};
// Inspect the encoded fixture itself: a filename/layout enum is not proof
// of single-channel Y versus RGB, nor of FLOAT32 rather than HALF storage.
inline QJsonObject exr_channels(const QString &path){
 QFile file(path);require(file.open(QIODevice::ReadOnly),"Cannot inspect own EXR header");const auto bytes=file.readAll();
 require(bytes.size()>=9&&qFromLittleEndian<quint32>(bytes.constData())==20000630,"Invalid own EXR header");qsizetype cursor=8;
 const auto text=[&](qsizetype &at){const auto end=bytes.indexOf(char(0),at);require(end>=at,"Unterminated own EXR header name");const auto name=bytes.mid(at,end-at);at=end+1;return name;};
 while(cursor<bytes.size()&&bytes[cursor]){
  const auto name=text(cursor),kind=text(cursor);require(bytes.size()-cursor>=4,"Truncated own EXR attribute");const auto length=qFromLittleEndian<quint32>(bytes.constData()+cursor);cursor+=4;require(length<=quint64(bytes.size()-cursor),"Truncated own EXR attribute payload");const auto end=cursor+length;
  if(name=="channels"){
   require(kind=="chlist","Invalid own EXR channels attribute");QJsonObject result;
   while(cursor<end&&bytes[cursor]){const auto channel=text(cursor);require(end-cursor>=16,"Truncated own EXR channel");const auto type=qFromLittleEndian<quint32>(bytes.constData()+cursor);require(type==2,"Own raw EXR fixture must store FLOAT32 components");result[QString::fromUtf8(channel)]=int(type);cursor+=16;}
   require(cursor<end&&cursor+1==end&&!result.isEmpty(),"Invalid own EXR channel list");return result;
  }
  cursor=end;
 }
 throw std::runtime_error("Own EXR fixture has no channels attribute");
}
inline void write_exr(const QString &path,Layout layout=Layout::RGBAZero){
 auto *buffer=IMB_allocImBuf(height_image_reference::fixture_width,height_image_reference::fixture_height,ImBufFlags::FloatData);
 require(buffer,"Cannot allocate raw Height EXR fixture");const auto release=qScopeGuard([&]{IMB_freeImBuf(buffer);});
 buffer->channels=layout==Layout::Y?1:layout==Layout::RGB?3:4;buffer->color_mode=layout==Layout::Y?ImColorMode::BW:layout==Layout::RGB?ImColorMode::RGB:ImColorMode::RGBA;buffer->ftype=IMB_FTYPE_OPENEXR;buffer->flags|=ImBufFlags::AlphaChannelPacked;
 const auto source=height_image_reference::fixture_pixels();
 for(std::size_t p=0;p<source.size();++p)for(int c=0;c<buffer->channels;++c)buffer->float_data_for_write()[p*buffer->channels+c]=c==3?0:source[p][c];
 require(IMB_save_image(buffer,path.toUtf8().constData(),ImBufFlags::FloatData),"Cannot write raw Height EXR fixture");
 const auto expected=layout==Layout::Y?QStringList{"Y"}:layout==Layout::RGB?QStringList{"B","G","R"}:QStringList{"A","B","G","R"};require(exr_channels(path).keys()==expected,"Own encoded EXR layout differs from the requested fixture");
}
inline DetachedNativeMaterial graph(const QString &image_path,bool linear=true,bool repeat=true,Mutation mutation=Mutation::None){
 auto source=native_raw_height_test::fixture(native_raw_height_test::Kind::Constant,.5f);
 bNodeTree *group=nullptr;bNode *bump=nullptr;
 for(auto &tree:source.main->nodetrees)for(auto &node:tree.nodes)if(STREQ(node.idname,"ShaderNodeBump")){require(!bump,"Image Height fixture needs one Bump");group=&tree;bump=&node;}
 require(group&&bump,"Image Height fixture has no group/Bump");
 auto *image=BKE_image_load(source.main.get(),image_path.toUtf8().constData());require(image,"Cannot read own raw Height image");
 STRNCPY(image->colorspace_settings.name,mutation==Mutation::ColorSpace?"sRGB":"Non-Color");image->alpha_mode=IMA_ALPHA_CHANNEL_PACKED;image->flag|=IMA_HIGH_BITDEPTH;
 if(mutation==Mutation::Generated)image->source=IMA_SRC_GENERATED;
 if(mutation==Mutation::Udims)image->source=IMA_SRC_TILED;
 auto *texture=bke::node_add_node(nullptr,*group,UString(mutation==Mutation::Environment?"ShaderNodeTexEnvironment":"ShaderNodeTexImage"));texture->id=&image->id;id_us_plus(texture->id);
 if(mutation!=Mutation::Environment){auto &settings=*static_cast<NodeTexImage *>(texture->storage);settings.projection=SHD_PROJ_FLAT;settings.interpolation=linear?SHD_INTERP_LINEAR:SHD_INTERP_CLOSEST;settings.extension=repeat?SHD_IMAGE_EXTENSION_REPEAT:SHD_IMAGE_EXTENSION_EXTEND;
 if(mutation==Mutation::Cubic)settings.interpolation=SHD_INTERP_CUBIC;
 if(mutation==Mutation::Clip)settings.extension=SHD_IMAGE_EXTENSION_CLIP;}
 auto *separate=bke::node_add_node(nullptr,*group,UString("ShaderNodeSeparateColor"));static_cast<NodeCombSepColor *>(separate->storage)->mode=mutation==Mutation::HSV?NODE_COMBSEP_COLOR_HSV:NODE_COMBSEP_COLOR_RGB;
 BKE_ntree_update_after_single_tree_change(*source.main,*group);
 bke::node_add_link(*group,*texture,socket(*texture,"Color",false),*separate,socket(*separate,"Color",true));
 auto &height=socket(*bump,"Height",true);if(height.link)bke::node_remove_link(group,*height.link);
 if(mutation==Mutation::Alpha||mutation==Mutation::ImplicitColor)bke::node_add_link(*group,*texture,socket(*texture,mutation==Mutation::Alpha?"Alpha":"Color",false),*bump,height);
 else if(mutation==Mutation::MathAfterRed){auto *math=bke::node_add_node(nullptr,*group,UString("ShaderNodeMath"));math->custom1=NODE_MATH_MULTIPLY;bke::node_add_link(*group,*separate,socket(*separate,"Red",false),*math,socket(*math,"Value",true));bke::node_add_link(*group,*math,socket(*math,"Value",false),*bump,height);}
 else bke::node_add_link(*group,*separate,socket(*separate,mutation==Mutation::Green?"Green":"Red",false),*bump,height);
 if(mutation==Mutation::CustomNormal){auto *normal=bke::node_add_node(nullptr,*group,UString("ShaderNodeNormalMap"));bke::node_add_link(*group,*normal,socket(*normal,"Normal",false),*bump,socket(*bump,"Normal",true));}
 BKE_ntree_update_after_single_tree_change(*source.main,*group);BKE_ntree_update_after_single_tree_change(*source.main,*source.material->nodetree);
 return source;
}
}
