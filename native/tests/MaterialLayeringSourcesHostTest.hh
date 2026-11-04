#pragma once
// Test-only real Host integration. Requires the separately admitted persistence-v3
// headers/production cohort; this file does not implement or enable map UI.
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include "FillTextureIO.hh"
#include "MaterialLayeringStacks.hh"
#include "ProjectOpenJob.hh"
#include "OrbitCamera.hh"
#include "MaterialLayeringSourceOpenCancelTest.hh"
#include "MaterialLayeringSourceSnapshots.hh"
#include "ResourceIO.hh"
#include "BKE_blendfile.hh"
#include "BKE_idprop.hh"
#include "BKE_report.hh"
#include "DNA_scene_types.h"
#include "BKE_global.hh"
#include "BKE_lib_id.hh"
#include "BLO_readfile.hh"
#include "DNA_packedFile_types.h"
#include "MEM_guardedalloc.h"
#include <future>

namespace bts::source_host_test {
using namespace blender;
using pd149_sources_test::require;
constexpr int source_width=8,source_height=4;
constexpr double bound=3e-5;
constexpr std::array<float,3> masks{.25f,.5f,.125f};
inline FillTexture::Pixel original_pixel(int,int);
inline std::array<double,3> multiply(const float3x3 &matrix,const std::array<double,3> &value){std::array<double,3> result{};for(int row=0;row<3;++row)for(int column=0;column<3;++column)result[row]+=double(matrix[column][row])*value[column];return result;}
// Independent repeating bilinear reference; source alpha is not a weight.
inline std::array<double,3> sample_original(const LayerMaterialInput &input,double u,double v){const double a=u*input.tiling[0],b=v*input.tiling[1],angle=input.rotation*3.14159265358979323846/180;u=std::cos(angle)*a-std::sin(angle)*b+input.offset[0];v=std::sin(angle)*a+std::cos(angle)*b+input.offset[1];u-=std::floor(u);v-=std::floor(v);const double x=u*source_width-.5,y=v*source_height-.5;const int ix=int(std::floor(x)),iy=int(std::floor(y));const double fx=x-ix,fy=y-iy;std::array<double,3> result{};for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){const auto pixel=original_pixel(((ix+dx)%source_width+source_width)%source_width,((iy+dy)%source_height+source_height)%source_height);const double weight=(dx?fx:1-fx)*(dy?fy:1-fy);for(int c=0;c<3;++c)result[c]+=weight*pixel[c];}return result;}
using Buffer=std::unique_ptr<ImBuf,decltype(&IMB_freeImBuf)>;

inline QString hash_bytes(const QByteArray &bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
inline QByteArray read_bytes(const QString &path){QFile file(path);require(file.open(QIODevice::ReadOnly),"Cannot read own Host source fixture/evidence");return file.readAll();}
inline void save_bytes(const QString &path,const QByteArray &bytes){QSaveFile file(path);require(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit(),"Cannot save own Host source evidence");}
inline void save_json(const QString &path,const QJsonObject &value){save_bytes(path,QJsonDocument(value).toJson());}
inline FillTexture::Pixel original_pixel(int x,int y){return {-.25f+.1234567f*x,.234567f+.111111f*y,2.345678f-.1234567f*x,(x+y)%3==0?0.f:(x+y)%3==1?.234567f:1.f};}
inline std::vector<FillTexture::Pixel> original_pixels(){std::vector<FillTexture::Pixel> values;for(int y=0;y<source_height;++y)for(int x=0;x<source_width;++x)values.push_back(original_pixel(x,y));return values;}
inline std::array<double,3> canonical(const FillTexture &source,const LayerMaterialInput &input,double u,double v){
 auto value=sample_original(input,u,v);
 if(!source.raw&&source.source_space=="Linear Rec.709")value=multiply(math::invert(ocio::ACESCG_TO_XYZ)*math::invert(ocio::XYZ_TO_REC709),value);
 else require(source.raw||source.source_space=="ACEScg","Unexpected source interpretation in independent oracle");
 return value;
}
inline Material &material(Main &main,std::uint32_t uid){for(Material &value:main.materials)if(value.id.session_uid==uid)return value;throw std::runtime_error("Host source material disappeared");}
inline QJsonObject geometry(const SceneSnapshot &scene){
 std::vector<QByteArray> sorted;
 for(const auto &triangle:scene.uv_triangles){require(triangle.material<scene.materials.size(),"Geometry material index invalid");QJsonArray corners,positions;for(int c=0;c<3;++c){corners.append(QJsonArray{triangle.corners[c][0],triangle.corners[c][1]});positions.append(QJsonArray{triangle.positions[c][0],triangle.positions[c][1],triangle.positions[c][2]});}sorted.push_back(QJsonDocument(QJsonObject{{"material",QString::fromStdString(scene.materials[triangle.material].name)},{"uv",corners},{"positions",positions}}).toJson(QJsonDocument::Compact));}
 std::sort(sorted.begin(),sorted.end());QByteArray joined;for(const auto &row:sorted){joined.append(row);joined.append('\n');}
 return {{"objects",qint64(scene.mesh_objects)},{"triangles",qint64(sorted.size())},{"positions_uv_material_sha256",hash_bytes(joined)}};
}
struct Aliases {
 std::map<std::string,std::pair<std::uint32_t,std::uintptr_t>> identities;
 QJsonArray rows;bool valid=true;
};
// Exact cache->GPU parity is separate from converted-color numeric bounds.
inline Aliases aliases(Main &main,const TextureDocument &document){
 Aliases result;std::map<std::string,const std::vector<FillTexture::Pixel> *> decoded;
 for(const auto &set:document.texture_sets()){
  auto &owner=material(main,set.material_session_uid);require(owner.nodetree,"Missing Host source tree");
  for(int input=0;input<4;++input)for(int channel:{0,1,2,5})if(const auto &source=set.material_layering.materials[input].textures[channel]){
   const auto key=gpu_texture_resource_key(*source);const std::string name="__BTS_LayerMaterial"+std::to_string(input)+(channel==0?"Color":"ScalarMap"+std::to_string(channel));
   auto *node=bke::node_find_node_by_name(*owner.nodetree,name.c_str());require(node&&node->id&&GS(node->id->name)==ID_IM,"Host source alias edge missing");auto *image=reinterpret_cast<Image *>(node->id);
   const auto pair=BKE_image_gpu_override_texture(image);require(pair.texture&&*pair.texture,"Host source alias is not resident");auto *texture=*pair.texture;
   require(source->pixels&&source->width==source_width&&source->height==source_height&&GPU_texture_width(texture)==source_width&&GPU_texture_height(texture)==source_height,"Host alias resized the original source");
   const auto identity=std::make_pair(image->id.session_uid,reinterpret_cast<std::uintptr_t>(texture));const auto [at,fresh]=result.identities.emplace(key,identity);require(at->second==identity,"Same typed key duplicated its Host alias across owners/inputs");
   const auto [pixels,new_pixels]=decoded.emplace(key,source->pixels.get());require(pixels->second==source->pixels.get(),"Same typed key duplicated decoded storage");
   if(!fresh)continue;
   Buffer cache(BKE_image_hosted_source_snapshot(image,1001),IMB_freeImBuf);require(cache&&cache->x==source_width&&cache->y==source_height&&cache->channels==4&&cache->float_data(),"Host alias lost immutable CPU cache");
   auto *gpu=static_cast<float *>(GPU_texture_read(texture,GPU_DATA_FLOAT,0));require(gpu,"Host source diagnostic GPU read failed");const auto release=qScopeGuard([&]{MEM_delete(gpu);});
   QJsonArray samples;double max_expected=0;bool finite=true,rgba_exact=true,raw_exact=true,alpha_exact=true;
   for(int y=0;y<source_height;++y)for(int x=0;x<source_width;++x){const auto index=std::size_t(y)*source_width+x;const auto actual=(*source->pixels)[index];auto expected=original_pixel(x,y);std::array<double,3> rgb{expected[0],expected[1],expected[2]};if(!source->raw&&source->source_space=="Linear Rec.709")rgb=multiply(math::invert(ocio::ACESCG_TO_XYZ)*math::invert(ocio::XYZ_TO_REC709),rgb);QJsonArray rgba;
    for(int c=0;c<4;++c){finite&=std::isfinite(actual[c])&&std::isfinite(cache->float_data()[index*4+c])&&std::isfinite(gpu[index*4+c]);rgba_exact&=actual[c]==cache->float_data()[index*4+c]&&actual[c]==gpu[index*4+c];rgba.append(std::isfinite(actual[c])?QJsonValue(actual[c]):QJsonValue());if(c<3&&std::isfinite(actual[c]))max_expected=std::max(max_expected,std::abs(double(actual[c])-rgb[c]));else if(c==3)alpha_exact&=actual[c]==expected[c];if(source->raw)raw_exact&=actual[c]==expected[c];}
    samples.append(rgba);
   }
   result.rows.append(QJsonObject{{"key",QString::fromStdString(key)},{"image_uid",qint64(image->id.session_uid)},{"gpu_handle",QString::number(reinterpret_cast<std::uintptr_t>(texture),16)},{"raw",source->raw},{"source_space",QString::fromStdString(source->source_space)},{"rgba",samples},{"maximum_independent_expected_error",max_expected},{"finite",finite},{"GPU_cache_decoded_exact",rgba_exact},{"raw_original_exact",raw_exact},{"independent_alpha_exact",alpha_exact}});
   result.valid&=finite&&rgba_exact&&raw_exact&&alpha_exact&&max_expected<=bound;
  }
 }
 require(result.identities.size()==3,"Host needs exactly raw/AP1/Rec709 typed aliases");std::set<std::uint32_t> unique;std::set<std::uintptr_t> textures;for(const auto &[key,value]:result.identities){unique.insert(value.first);textures.insert(value.second);}require(unique.size()==3&&textures.size()==3,"Different typed interpretations shared an alias Image/GPU allocation");int marked=0;for(const Image &image:main.images)marked+=bool(image.runtime->gpuflag&IMA_HOSTED_IMMUTABLE_SOURCE);require(marked==3,"Main retained duplicate runtime source aliases");return result;
}

class Test final {
 BlenderHost &host_;QString directory_,project_,mode_,source_path_;QTimer timer_;QEventLoop loop_;QElapsedTimer elapsed_;ProjectOpenJob opener_;OrbitCamera camera_;
 std::optional<TextureDocument> document_;std::vector<StudioAsset> resources_;std::future<std::array<std::shared_ptr<const FillTexture>,3>> decode_;
 QJsonObject proof_,oracle_;QString error_;int stage_=0;bool busy_=false,done_=false,cleaned_=false;Main *old_main_=nullptr;Aliases candidate_;std::uint64_t recipes_=0;int ready_callbacks_=0,stage_callbacks_=0;
 void checkpoint(){proof_["stage"]=stage_;proof_["mode"]=mode_;proof_["native_pid"]=qint64(QCoreApplication::applicationPid());save_json(directory_+"/source-host.json",proof_);}
 void finish(const QString &error={}){timer_.stop();error_=error;done_=true;loop_.quit();}
 void create(const QString &mesh){NewProjectOptions options;options.absolute_mesh_path=mesh.toStdString();options.resolution=DocumentResolution::Pixels128;options.uv_workflow=UvWorkflow::PreserveTiles;const auto summary=host_.new_project(mesh.toStdString(),qEnvironmentVariable("BTS_ENVIRONMENT").toStdString(),&options);document_.emplace(options,summary,host_.scene_snapshot());require(document_->texture_sets().size()==2,"Host fixture requires two actual material sets");for(const auto &set:document_->texture_sets())require(set.tiles==std::set<int>{1001,1002},"Host fixture requires both UDIMs per set");require(host_.color_state()["native_acescg"].toBool(),"Fixture must use the actual New-project AP1 context");const auto scene=host_.scene_snapshot();require(scene.has_bounds,"Host fixture has no framing bounds");camera_.frame_bounds({scene.minimum[0],scene.minimum[1],scene.minimum[2]},{scene.maximum[0],scene.maximum[1],scene.maximum[2]},{1000,700});}
 QJsonObject first_draw_witness(){
  QJsonArray mask_rows;Aliases source_aliases;
  host_.with_gpu([&]{source_aliases=aliases(*G_MAIN,*document_);});
  const std::array<std::array<int,2>,3> xy{{{32,32},{64,64},{96,96}}};
  for(const auto &set:document_->texture_sets())for(int slot=0;slot<3;++slot){
   QJsonObject identity;host_.with_gpu([&]{
    const auto name="__BTS_UserPixels"+std::to_string(set.material_layering.mask_slots[slot]);
    auto *node=bke::node_find_node_by_name(*material(*G_MAIN,set.material_session_uid).nodetree,name.c_str());
    require(node&&node->id&&GS(node->id->name)==ID_IM,"First draw mask image edge missing");auto *image=reinterpret_cast<Image *>(node->id);
    const auto cached=BKE_image_gpu_override_texture(image);auto *texture=cached.texture?*cached.texture:nullptr;
    require(texture,"First draw mask was not published before residency");
    identity={{"set",set.id.toString(QUuid::WithoutBraces)},{"slot",slot},{"image",image->id.name+2},{"image_uid",qint64(image->id.session_uid)},{"gpu_handle",QString::number(reinterpret_cast<std::uintptr_t>(texture),16)}};
   });
   for(int tile:{1001,1002}){auto row=identity;row["tile"]=tile;QJsonArray samples;
    const auto values=host_.diagnostic_gpu_pixels(set.material_session_uid,7+set.material_layering.mask_slots[slot],xy,tile);
    require(values.size()==xy.size(),"First draw mask witness shape differs");
    for(const auto &pixel:values){QJsonArray rgba;for(float value:pixel)rgba.append(std::isfinite(value)?QJsonValue(value):QJsonValue());samples.append(rgba);}
    row["rgba"]=samples;mask_rows.append(row);
   }
  }
  return {{"masks",mask_rows},{"source_aliases",source_aliases.rows},{"source_aliases_valid",source_aliases.valid}};
 }
 bool frame_ready(){
  // Keep the valid preview -> flush -> first draw regression: residency must
  // preserve already published GPU pixels, not depend on a later recomposite.
  host_.flush_texture_updates();const bool witness=mode_=="writer"&&!proof_.contains("first_draw_before");
  if(witness){if(host_.texture_updates_pending())return false;proof_["first_draw_before"]=first_draw_witness();checkpoint();}
  ViewFrame frame;frame.pane={ViewKind::Scene,0,0,1000,700};const auto view=camera_.view_matrix(),projection=camera_.projection_matrix({1000,700});std::copy_n(view.constData(),16,frame.view.begin());std::copy_n(projection.constData(),16,frame.projection.begin());const auto clip=camera_.clipping_range();frame.clipping={clip.first,clip.second};host_.draw(std::span(&frame,1),1000,700);
  if(witness){proof_["first_draw_after"]=first_draw_witness();proof_["first_draw_pixels_handles_preserved"]=proof_["first_draw_before"]==proof_["first_draw_after"];checkpoint();require(proof_["first_draw_pixels_handles_preserved"].toBool()&&proof_["first_draw_after"].toObject()["source_aliases_valid"].toBool(),"First draw changed published mask/source pixels or GPU handles");}
  return !host_.texture_updates_pending();
 }
 void fixture(){
  const auto mesh=directory_+"/fixture.obj";save_bytes(directory_+"/fixture.mtl","newmtl OwnerA\nKd .4 .4 .4\nnewmtl OwnerB\nKd .3 .3 .3\n");
  QByteArray obj="mtllib fixture.mtl\n";int base=0;
  for(int owner=0;owner<2;++owner){obj+="o Owner"+QByteArray::number(owner)+"\nusemtl "+QByteArray(owner?"OwnerB":"OwnerA")+"\n";for(int tile=0;tile<2;++tile){const int x=owner*3+tile;obj+="v "+QByteArray::number(x)+" 0 0\nv "+QByteArray::number(x+1)+" 0 0\nv "+QByteArray::number(x+1)+" 1 0\nv "+QByteArray::number(x)+" 1 0\n";obj+="vt "+QByteArray::number(tile)+" 0\nvt "+QByteArray::number(tile+1)+" 0\nvt "+QByteArray::number(tile+1)+" 1\nvt "+QByteArray::number(tile)+" 1\nf";for(int k=1;k<=4;++k)obj+=" "+QByteArray::number(base+k)+"/"+QByteArray::number(base+k);obj+='\n';base+=4;}}
  save_bytes(mesh,obj);create(mesh);source_path_=directory_+"/original.exr";
  host_.with_gpu([&]{auto *image=reinterpret_cast<Image *>(BKE_id_new(G_MAIN,ID_IM,"Own source encoder"));require(image,"Source encoder missing");const auto remove=qScopeGuard([&]{BKE_id_free(G_MAIN,image);});image->source=IMA_SRC_GENERATED;image->type=IMA_TYPE_UV_TEST;image->alpha_mode=IMA_ALPHA_CHANNEL_PACKED;auto *tile=static_cast<ImageTile *>(image->tiles.first);tile->gen_x=source_width;tile->gen_y=source_height;image->runtime->gpuflag|=IMA_HOSTED_IMMUTABLE_SOURCE;
   FillTexture source;source.asset=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();source.hash=std::string(64,'e');source.name="Own original";source.raw=true;source.width=source_width;source.height=source_height;source.pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(original_pixels());Buffer header(material_layer_source_pixel_view(source),IMB_freeImBuf);require(BKE_image_assign_hosted_immutable_source(image,1001,header.get())&&BKE_image_memorypack(image),"Cannot encode own original EXR");const auto *part=static_cast<ImagePackedFile *>(image->packedfiles.first);require(part&&part->packedfile&&part->packedfile->size>0,"Own original encoding empty");const auto *packed=part->packedfile;const QByteArray bytes(static_cast<const char *>(packed->data),packed->size);save_bytes(source_path_,bytes);
   StudioAsset asset;asset.name="Owned original raw";asset.filename="original.exr";asset.path=source_path_;asset.usages={"texture"};asset.scope=AssetScope::Project;asset.bytes=bytes;resources_.push_back(asset);asset.id=QUuid::createUuid();asset.name="Owned original AP1";resources_.push_back(asset);asset.id=QUuid::createUuid();asset.name="Owned original Rec709";resources_.push_back(asset);proof_["original_sha256"]=hash_bytes(bytes);
  });
  decode_=std::async(std::launch::async,[assets=resources_]{return std::array{decode_fill_texture(assets[0],true),decode_fill_texture(assets[1],false,"ACEScg"),decode_fill_texture(assets[2],false,"Linear Rec.709")};});
 }
 void configure(const std::array<std::shared_ptr<const FillTexture>,3> &sources){
  for(const auto &description:document_->texture_sets()){auto &set=document_->set(description.material_session_uid);configure_material_layering(set,true);
   for(int slot=0;slot<3;++slot){set.users[set.material_layering.mask_slots[slot]].precision=ChannelPrecision::L32F;select_material_substack(set,slot);TextureDocument::add_fill(set);TextureDocument::layer(set,set.selected_layer).users[set.material_layering.mask_slots[slot]].value=masks[slot];}
   select_material_substack(set,-1);for(int input=0;input<4;++input){auto &value=set.material_layering.materials[input];value.values.base_color={.125f,.25f,.5f,1};value.values.metallic=.25f;value.values.roughness=.5f;value.values.ambient_occlusion=.75f;value.textures[0]=sources[input%2?2:1];value.textures[1]=value.textures[2]=value.textures[5]=sources[0];}
   set.material_layering.materials[1].tiling={2,-1};set.material_layering.materials[1].offset={.25,.5};set.material_layering.materials[1].rotation=90;set.material_layering.materials[2].tiling={-1,2};set.material_layering.materials[2].offset={-.5,.25};set.material_layering.materials[2].rotation=-90;
   validate_material_substacks(set);host_.prepare_paint_texture(set);host_.set_shader_parameters(set);require(host_.preview_layer_edit(set,complete_composite_channels,false),"Host source fixture rejected GPU publication; no CPU fallback");
  }
 }
 QJsonObject graph_numeric(Main &main,const TextureDocument &document,const pd149_sources_test::StockSavedImages *stock=nullptr){
  QJsonArray rows;double maximum=0;bool finite=true;std::size_t count=0;
  host_.with_gpu([&]{for(const auto &set:document.texture_sets())for(const auto output:{pd149_sources_test::Output::Color,pd149_sources_test::Output::Metallic,pd149_sources_test::Output::Roughness,pd149_sources_test::Output::AO}){
   auto tap=stock?std::make_unique<pd149_sources_test::SourceGpuTap>(material(main,set.material_session_uid),output,source_width,source_height,*stock):std::make_unique<pd149_sources_test::SourceGpuTap>(material(main,set.material_session_uid),output,source_width,source_height);
   for(int tile:{1001,1002}){const auto values=tap->sample(tile);require(values.size()==source_width*source_height,"Host graph sample shape differs");QJsonArray pixels;for(int y=0;y<source_height;++y)for(int x=0;x<source_width;++x){const auto &actual=values[std::size_t(y)*source_width+x];std::array<double,3> expected{};double ao=0;const std::array<double,4> weights{.328125,.109375,.4375,.125};
    for(int input=0;input<4;++input){const auto &value=set.material_layering.materials[input];const double u=(x+.5)/source_width+tile-1001,v=(y+.5)/source_height;const double a=std::clamp(canonical(*value.textures[5],value,u,v)[0],0.,1.);ao+=weights[input]*a;if(output==pd149_sources_test::Output::Color){const auto rgb=canonical(*value.textures[0],value,u,v);for(int c=0;c<3;++c)expected[c]+=weights[input]*rgb[c];}else if(output!=pd149_sources_test::Output::AO){const int channel=output==pd149_sources_test::Output::Metallic?1:2;const auto scalar=std::clamp(canonical(*value.textures[channel],value,u,v)[0],0.,1.);for(auto &c:expected)c+=weights[input]*scalar;}}
    if(output==pd149_sources_test::Output::Color)for(auto &c:expected)c*=ao;else if(output==pd149_sources_test::Output::AO)expected.fill(ao);QJsonArray rgba,wanted;for(int c=0;c<4;++c){finite&=std::isfinite(actual[c]);rgba.append(std::isfinite(actual[c])?QJsonValue(actual[c]):QJsonValue());if(c<3){wanted.append(expected[c]);if(std::isfinite(actual[c]))maximum=std::max(maximum,std::abs(double(actual[c])-expected[c]));++count;}}pixels.append(QJsonObject{{"xy",QJsonArray{x,y}},{"actual",rgba},{"expected",wanted}});
   }rows.append(QJsonObject{{"set",set.id.toString(QUuid::WithoutBraces)},{"output",int(output)},{"tile",tile},{"pixels",pixels}});}
  }});
  return {{"raw_graph_samples",rows},{"components",qint64(count)},{"finite",finite},{"maximum_expected_error",maximum},{"fixed_bound",bound}};
 }
 QJsonObject numeric(){
  host_.with_gpu([&]{QJsonArray inputs;for(const auto &set:document_->texture_sets())for(const auto &node:material(*G_MAIN,set.material_session_uid).nodetree->nodes)if(STREQ(node.idname,"ShaderNodeTexImage")&&node.id&&GS(node.id->name)==ID_IM){auto *image=reinterpret_cast<Image *>(node.id);const auto cached=BKE_image_gpu_override_texture(image);auto *texture=cached.texture?*cached.texture:nullptr;inputs.append(QJsonObject{{"set",set.id.toString(QUuid::WithoutBraces)},{"node",node.name},{"image",image->id.name+2},{"image_uid",qint64(image->id.session_uid)},{"resident",bool(texture)},{"width",texture?GPU_texture_width(texture):0},{"height",texture?GPU_texture_height(texture):0}});}proof_["numeric_input_residency"]=inputs;checkpoint();});
  auto result=graph_numeric(*G_MAIN,*document_);QJsonArray mask_rows;bool finite=result["finite"].toBool();const auto count=result["components"].toInteger();const auto maximum=result["maximum_expected_error"].toDouble();
  const std::array<std::array<int,2>,3> xy{{{32,32},{64,64},{96,96}}};double mask_error=0;
  for(const auto &set:document_->texture_sets())for(int slot=0;slot<3;++slot)for(int tile:{1001,1002}){const auto values=host_.diagnostic_gpu_pixels(set.material_session_uid,7+set.material_layering.mask_slots[slot],xy,tile);require(values.size()==xy.size(),"Actual Host mask witness missing");QJsonArray actual;for(const auto &pixel:values){QJsonArray rgba;for(float value:pixel){finite&=std::isfinite(value);rgba.append(std::isfinite(value)?QJsonValue(value):QJsonValue());}actual.append(rgba);if(std::isfinite(pixel[0]))mask_error=std::max(mask_error,std::abs(double(pixel[0])-masks[slot]));}mask_rows.append(QJsonObject{{"set",set.id.toString(QUuid::WithoutBraces)},{"slot",slot},{"tile",tile},{"expected",masks[slot]},{"actual_rgba",actual}});}
  result["mask_checks"]=mask_rows;result["finite"]=finite;result["mask_error"]=mask_error;proof_["numeric"]=result;checkpoint();require(finite&&count==1536&&maximum<=bound&&mask_error<=bound,"Host graph/mask independent numeric oracle failed");return result;
 }
 QJsonObject live_fingerprint()const{
  // Cache descriptors only: no Image getter, upload, renderer, pixel read or
  // aliases() call. Cancel must preserve the currently adopted Main verbatim.
  QJsonArray images,edges;
  for(Image &image:G_MAIN->images){const auto cached=BKE_image_gpu_override_texture(&image);images.append(QJsonObject{{"uid",qint64(image.id.session_uid)},{"name",image.id.name+2},{"gpu",QString::number(reinterpret_cast<std::uintptr_t>(cached.texture?*cached.texture:nullptr),16)},{"mapping",QString::number(reinterpret_cast<std::uintptr_t>(cached.tile_mapping?*cached.tile_mapping:nullptr),16)}});}
  for(const auto &set:document_->texture_sets())for(const auto &node:material(*G_MAIN,set.material_session_uid).nodetree->nodes)if(node.id&&GS(node.id->name)==ID_IM)edges.append(QJsonObject{{"set",set.id.toString(QUuid::WithoutBraces)},{"node",node.name},{"image_uid",qint64(node.id->session_uid)}});
  return {{"main",QString::number(reinterpret_cast<std::uintptr_t>(G_MAIN),16)},{"document",document_->to_json()},{"images",images},{"material_image_edges",edges}};
 }
 QJsonObject detached_stock(const QString &path,const QString &evidence_key){
  auto *const live=G_MAIN;const auto live_before=live_fingerprint();
  ReportList reports{};BKE_reports_init(&reports,RPT_STORE);const auto clear_reports=qScopeGuard([&]{BKE_reports_free(&reports);});
  BlendFileReadReport report{};report.reports=&reports;
  std::unique_ptr<BlendFileData,decltype(&BLO_blendfiledata_free)> file(BLO_read_from_file(path.toUtf8().constData(),BLO_READ_SKIP_USERDEF,&report),BLO_blendfiledata_free);
  require(file&&file->main&&file->curscene&&file->main!=live&&G_MAIN==live,"Detached saved project read replaced or lacked its Main/Scene");
  auto &main=*file->main;auto &scene=*file->curscene;
  // All ordinary uploads and diagnostic programs die on this owner BEFORE the
  // detached Main. Scope also covers the first oracle exception.
  bool gpu_cleaned=false;const auto retire_gpu=[&]{if(!gpu_cleaned){host_.with_gpu([&]{for(Image &image:main.images)BKE_image_free_gputextures(&image);});gpu_cleaned=true;}};const auto clear_gpu=qScopeGuard([&]{retire_gpu();});
  const auto property=[](IDProperty *root,const char *key,int type){auto *value=root?IDP_GetPropertyFromGroup(root,key):nullptr;require(value&&value->type==type,"Detached project property type/reference missing");return value;};
  auto *root=scene.id.properties;auto *json=property(root,"bts_document",IDP_STRING);require(json->len>0,"Detached document is empty");
  QJsonParseError error;const auto metadata=QJsonDocument::fromJson(QByteArray(IDP_string_get(json)),&error);require(error.error==QJsonParseError::NoError&&metadata.isObject(),"Detached document is not JSON");
  auto *sets=property(root,"bts_sets",IDP_GROUP);std::vector<TextureSetBinding> bindings;
  for(const auto &row:metadata.object()["texture_sets"].toArray()){
   const QUuid id(row.toObject()["id"].toString());require(!id.isNull(),"Detached set UUID missing");const auto key=id.toString(QUuid::WithoutBraces).toStdString();auto *ref=property(sets,key.c_str(),IDP_ID);auto *target=IDP_ID_get(ref);bool owned=false;for(Material &value:main.materials)owned|=&value.id==target;require(target&&GS(target->name)==ID_MA&&owned,"Detached set has no owned material ID reference");bindings.push_back({id,target->session_uid,target->name+2});
  }
  auto document=TextureDocument::from_json(metadata.object(),bindings);ProjectResourceImages resources;const auto assets=read_project_assets(scene,&resources);resolve_fill_textures(document,assets);
  require(bindings.size()==2&&assets.size()==3&&resources.by_key.size()==3&&resources.sha256.size()==3,"Detached source set/resource shape changed");
  for(const auto &asset:assets)require(resources.by_key.contains(asset.id.toString(QUuid::WithoutBraces).toStdString()),"Detached resource UUID has no manifest Image reference");
  require(validate_packed_material_layer_sources(main,scene,document,resources),"Detached stock graph lacks validated source provenance");
  require(document.to_json()==oracle_["document"].toObject(),"Detached stock typed descriptors/document changed");
  const auto original_hash=oracle_["original_sha256"].toString();require(!original_hash.isEmpty(),"Detached audit lacks the independent original hash");
  auto *carrier=resources.by_key.begin()->second;require(carrier,"Detached resource carrier missing");int original_payloads=0,runtime_aliases=0;QJsonArray resource_rows,owned_rows;
  for(Image &image:main.images){runtime_aliases+=bool(image.runtime->gpuflag&IMA_HOSTED_IMMUTABLE_SOURCE)||STRPREFIX(image.id.name+2,"BTS Layer Source ");for(const ImagePackedFile &part:image.packedfiles)if(part.packedfile&&part.packedfile->data&&part.packedfile->size>0){const auto *packed=part.packedfile;const auto digest=QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(static_cast<const char *>(packed->data),packed->size),QCryptographicHash::Sha256).toHex());if(digest==original_hash){++original_payloads;require(&image==carrier,"Original packed payload belongs to another Image");}}}
  for(const auto &[key,image]:resources.by_key){require(image==carrier&&resources.sha256.at(key)==original_hash,"Resource UUID keys do not share the original carrier/hash");resource_rows.append(QJsonObject{{"key",QString::fromStdString(key)},{"carrier_uid",qint64(image->id.session_uid)},{"sha256",resources.sha256.at(key)}});}
  pd149_sources_test::StockSavedImages stock;stock.audited.insert(carrier);
  for(const auto &set:document.texture_sets()){
   auto &owner=material(main,set.material_session_uid);
   for(int input=0;input<4;++input)for(int channel:{0,1,2,5}){
    const auto name="__BTS_LayerMaterial"+std::to_string(input)+(channel==0?"Color":"ScalarMap"+std::to_string(channel));auto *node=bke::node_find_node_by_name(*owner.nodetree,name.c_str());require(node&&STREQ(node->idname,"ShaderNodeTexImage")&&node->id==&carrier->id,"Saved owned TexImage node does not reference the manifest carrier");owned_rows.append(QJsonObject{{"set",set.id.toString(QUuid::WithoutBraces)},{"node",node->name},{"carrier_uid",qint64(carrier->id.session_uid)}});
   }
   for(int slot:set.material_layering.mask_slots){auto *node=bke::node_find_node_by_name(*owner.nodetree,("__BTS_UserPixels"+std::to_string(slot)).c_str());require(node&&node->id&&GS(node->id->name)==ID_IM,"Saved stock mask node missing");auto *image=reinterpret_cast<Image *>(node->id);bool owned=false;for(Image &candidate:main.images)owned|=&candidate==image;require(owned,"Saved stock mask is outside its Main");stock.audited.insert(image);}
  }
  auto *marker=property(root,"bts_material_layer_sources",IDP_STRING);const auto manifest=QJsonDocument::fromJson(QByteArray(IDP_string_get(marker))).object();
  QJsonObject evidence{{"path",path},{"original_packed_payload_count",original_payloads},{"original_sha256",original_hash},{"runtime_alias_count",runtime_aliases},{"resource_refs",resource_rows},{"owned_source_refs",owned_rows},{"binding_rows",manifest["bindings"].toArray().size()},{"whitelisted_images",qint64(stock.audited.size())},{"native_inspection_restore_called",false},{"canonical_alias_adaptation_called",false}};
  proof_[evidence_key]=evidence;checkpoint();require(original_payloads==1&&runtime_aliases==0&&owned_rows.size()==32&&manifest["bindings"].toArray().size()==32&&stock.audited.size()==7,"Detached original payload/alias/source edge audit failed");
  host_.with_gpu([&]{
   // This is the explicit ordinary stock carrier, never the hosted adapter.
   const auto ordinary=BKE_image_get_gpu_material_texture(carrier,nullptr,true);auto *texture=ordinary.texture?*ordinary.texture:nullptr;require(texture,"Detached stock carrier failed ordinary GPU upload");
   void *lock=nullptr;Buffer buffer(BKE_image_acquire_ibuf(carrier,nullptr,&lock),IMB_freeImBuf);const auto release=qScopeGuard([&]{BKE_image_release_ibuf(carrier,buffer.release(),lock);});
   require(buffer&&buffer->x==source_width&&buffer->y==source_height&&buffer->channels==4&&buffer->float_data(),"Detached stock carrier decoded shape/float storage differs");
   auto *pixels=static_cast<float *>(GPU_texture_read(texture,GPU_DATA_FLOAT,0));require(pixels,"Detached stock carrier readback missing");const auto free_pixels=qScopeGuard([&]{MEM_delete(pixels);});bool finite=true,exact=true;QJsonArray rgba;
   for(int y=0;y<source_height;++y)for(int x=0;x<source_width;++x){const auto expected=original_pixel(x,y);const auto index=std::size_t(y)*source_width+x;QJsonArray row;for(int c=0;c<4;++c){const float gpu=pixels[index*4+c],cpu=buffer->float_data()[index*4+c];finite&=std::isfinite(cpu)&&std::isfinite(gpu);exact&=gpu==expected[c]&&cpu==expected[c];row.append(std::isfinite(gpu)?QJsonValue(gpu):QJsonValue());}rgba.append(row);}
   evidence["carrier_gpu_format"]=GPU_texture_format_name(GPU_texture_format(texture));evidence["carrier_gpu_width"]=GPU_texture_width(texture);evidence["carrier_gpu_height"]=GPU_texture_height(texture);evidence["carrier_raw_rgba"]=rgba;evidence["carrier_raw_CPU_GPU_original_exact"]=exact;evidence["carrier_finite"]=finite;proof_[evidence_key]=evidence;checkpoint();
   require(carrier->flag&IMA_HIGH_BITDEPTH,"Detached stock carrier lost its full float flag");require(GPU_texture_format(texture)==gpu::TextureFormat::SFLOAT_32_32_32_32&&GPU_texture_width(texture)==source_width&&GPU_texture_height(texture)==source_height&&finite&&exact,"Detached ordinary stock carrier quantized or changed raw RGBA");
  });
  evidence["numeric"]=graph_numeric(main,document,&stock);retire_gpu();file.reset();evidence["detached_GPU_retired_before_Main_free"]=gpu_cleaned;evidence["live_owner_unchanged"]=G_MAIN==live&&live_fingerprint()==live_before;proof_[evidence_key]=evidence;checkpoint();const auto numeric=evidence["numeric"].toObject();require(numeric["finite"].toBool()&&numeric["components"].toInteger()==1536&&numeric["maximum_expected_error"].toDouble()<=bound,"Detached stock graph independent numeric oracle failed");require(evidence["live_owner_unchanged"].toBool(),"Detached stock audit changed the live owner");return evidence;
 }
 void begin_open(){
  require(!QFileInfo::exists(QFileInfo(project_).dir().filePath("original.exr")),"Cold child still has original EXR file");oracle_=QJsonDocument::fromJson(read_bytes(QFileInfo(project_).dir().filePath("host-oracle.json"))).object();require(oracle_["schema"]==1&&oracle_["writer_pid"].toInteger()>0&&oracle_["writer_pid"].toInteger()!=QCoreApplication::applicationPid(),"Cold child needs a distinct actual writer PID");require(hash_bytes(read_bytes(project_))==oracle_["project_sha256"].toString(),"Cold packed file changed");
  create(QFileInfo(project_).dir().filePath("fixture.obj"));proof_["saved_detached_stock"]=detached_stock(project_,"saved_detached_stock");proof_["stock_graph_GPU_tested"]=true;old_main_=G_MAIN;
  opener_.start([path=project_.toStdString()](auto cancel,auto progress){return PendingProject::prepare(path,cancel,progress);},[this]{return G_MAIN==old_main_;},[this](ProjectOpenResult result){
   require(result.pending&&result.pending->layer_sources&&result.gpu&&result.gpu->ready(),"Real Host callback lacked ready source staging");require(G_MAIN==old_main_&&result.pending->file->main!=old_main_,"Pending source staging changed live Main early");
   host_.with_gpu([&]{candidate_=aliases(*result.pending->file->main,*result.pending->document);});proof_["candidate_aliases"]=candidate_.rows;checkpoint();require(candidate_.valid,"Candidate source RGBA oracle failed");host_.validate_project_adoption(*result.pending,result.gpu.get());resources_=result.pending->resources;document_=host_.adopt_project(std::move(result.pending),project_.toStdString(),std::move(result.gpu));++ready_callbacks_;
   host_.with_gpu([&]{const auto adopted=aliases(*G_MAIN,*document_);proof_["adopted_aliases"]=adopted.rows;checkpoint();require(adopted.valid,"Adopted source RGBA oracle failed");require(candidate_.identities==adopted.identities,"Adoption replaced already-staged source aliases/GPU allocations");});
   require(G_MAIN!=old_main_,"Actual Host never adopted the pending Main");
  },[this](const QString &error){finish(error);},[this](const QString &message){proof_["last_open_progress"]=message;if(message=="Preparing isolated project source GPU resources…"||message=="Preparing isolated native Height GPU sources…")++stage_callbacks_;});
 }
 void advance(){
  checkpoint();require(elapsed_.elapsed()<120000,"Host source persistence diagnostic timed out");require(PaintPixels::recipe_materializations()==recipes_,"Host source path materialized CPU paint recipes");
  if(mode_=="writer"){
   if(stage_==0){fixture();stage_=1;return;}
   if(stage_==1){if(decode_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;configure(decode_.get());stage_=2;return;}
   if(stage_==2){if(!frame_ready())return;Aliases live;host_.with_gpu([&]{live=aliases(*G_MAIN,*document_);});proof_["live_aliases"]=live.rows;checkpoint();require(live.valid,"Writer source RGBA oracle failed");numeric();proof_["geometry"]=geometry(host_.scene_snapshot());proof_["color_state"]=host_.color_state();host_.begin_save_project(*document_,project_.toStdString(),resources_);stage_=3;return;}
   if(stage_==3){if(!host_.poll_save_project())return;require(!host_.save_project_pending(),"Host Save still pending after completion");QJsonObject oracle{{"schema",1},{"writer_pid",qint64(QCoreApplication::applicationPid())},{"document",document_->to_json()},{"geometry",proof_["geometry"]},{"numeric",proof_["numeric"]},{"original_sha256",proof_["original_sha256"]},{"project_sha256",hash_bytes(read_bytes(project_))}};save_json(directory_+"/host-oracle.json",oracle);proof_["async_host_save"]=true;finish();return;}
  }else{
   if(stage_==0){begin_open();stage_=1;return;}
   if(stage_==1){if(opener_.pending())return;require(ready_callbacks_==1&&stage_callbacks_==1,"Cold child did not traverse exactly one isolated Host source stage/adoption");stage_=2;return;}
   if(stage_==2){if(!frame_ready())return;proof_["geometry"]=geometry(host_.scene_snapshot());proof_["color_state"]=host_.color_state();require(document_->to_json()==oracle_["document"].toObject(),"Fresh Open changed typed descriptors/transforms/document");require(proof_["geometry"]==oracle_["geometry"],"Fresh Open changed original geometry/UVs");numeric();const auto before=oracle_["numeric"].toObject()["raw_graph_samples"].toArray(),after=proof_["numeric"].toObject()["raw_graph_samples"].toArray();require(before.size()==16&&after.size()==before.size(),"Cold graph checkpoint shape differs");double restored_error=0;for(int row=0;row<after.size();++row){auto old=before[row].toObject(),now=after[row].toObject();const auto a=old.take("pixels").toArray(),b=now.take("pixels").toArray();require(old==now&&a.size()==32&&b.size()==a.size(),"Cold graph checkpoint identity differs");for(int pixel=0;pixel<b.size();++pixel){const auto prior=a[pixel].toObject(),current=b[pixel].toObject();require(prior["xy"]==current["xy"]&&prior["expected"]==current["expected"],"Independent cold oracle changed");const auto pa=prior["actual"].toArray(),pb=current["actual"].toArray();require(pa.size()==4&&pb.size()==4,"Malformed cold checkpoint RGBA");for(int c=0;c<4;++c){require(pa[c].isDouble()&&pb[c].isDouble()&&std::isfinite(pa[c].toDouble())&&std::isfinite(pb[c].toDouble()),"Nonfinite cold checkpoint RGBA");restored_error=std::max(restored_error,std::abs(pa[c].toDouble()-pb[c].toDouble()));}}}proof_["maximum_cold_graph_error"]=restored_error;checkpoint();require(restored_error<=bound,"Fresh Open graph samples differ at fixed bound");proof_["fresh_source_hidden_open"]=true;proof_["same_staged_adopted_aliases"]=true;proof_["ready_callbacks"]=ready_callbacks_;proof_["stage_callbacks"]=stage_callbacks_;stage_=3;return;}
   if(stage_==3){proof_["cancellation"]=test_material_layering_source_open_cancel(host_,project_,[this]{return live_fingerprint();});proof_["cancellation_tested"]=true;checkpoint();host_.begin_save_project(*document_,(directory_+"/resaved-source.blend").toStdString(),resources_);stage_=4;return;}
   if(stage_==4){if(!host_.poll_save_project())return;require(!host_.save_project_pending(),"Second Host Save did not complete");proof_["second_async_host_save"]=true;proof_["resaved_detached_stock"]=detached_stock(directory_+"/resaved-source.blend","resaved_detached_stock");checkpoint();finish();return;}
  }
 }
 public:
 Test(BlenderHost &host,QString directory,QString project,QString mode):host_(host),directory_(std::move(directory)),project_(std::move(project)),mode_(std::move(mode)),opener_(host){require(mode_=="writer"||mode_=="cold","Choose explicit source Host writer/cold mode");QDir().mkpath(directory_);require(!project_.isEmpty()&&!qEnvironmentVariable("BTS_ENVIRONMENT").isEmpty(),"Host source test needs explicit project and HDR");recipes_=PaintPixels::recipe_materializations();proof_={{"kind","pd149-real-Host-source-persistence-positive"},{"test_fixture_authored",true},{"maps_UI_enabled",false},{"stock_graph_GPU_tested",false},{"cancellation_tested",false},{"fault_injection_tested",false},{"physical_oom_tested",false},{"performance_claim",false}};}
 ~Test(){if(!cleaned_)qFatal("Host source fixture escaped explicit owner cleanup");}
 void cleanup(){opener_.shutdown();if(decode_.valid())try{decode_.get();}catch(...){}document_.reset();resources_.clear();candidate_={};cleaned_=true;}
 int run(){elapsed_.start();timer_.setInterval(1);QObject::connect(&timer_,&QTimer::timeout,&loop_,[this]{if(busy_||done_)return;const auto release=qScopeGuard([&]{busy_=false;});busy_=true;try{advance();}catch(const std::exception &error){finish(QString::fromUtf8(error.what()));}catch(...){finish("Unknown Host source fixture failure");}});timer_.start();loop_.exec();cleanup();proof_["passed"]=error_.isEmpty();proof_["error"]=error_;proof_["owner_cleanup_complete"]=true;proof_["async_Open_worker_and_isolated_owner_GPU"]=mode_=="cold"&&error_.isEmpty();checkpoint();return error_.isEmpty()?0:1;}
};
}
namespace bts {
inline int run_material_layering_source_host_test(BlenderHost &host,const QString &directory,const QString &project){source_host_test::Test test(host,directory,project,qEnvironmentVariable("BTS_PD149_SOURCE_HOST_MODE"));return test.run();}
}
