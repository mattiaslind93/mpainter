// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QCryptographicHash>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QScopeGuard>
#include "BlenderHost.hh"
#include "BlendMaterial.hh"
#include "NativeMaterialAsset.hh"
#include "NativeMaterialArchive.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialGpuProgram.hh"
#include "NativeMaterialImageResource.hh"
#include "NativeMaterialImages.hh"
#include "NativeMaterialImageUpload.hh"
#include "NativeMaterialResource.hh"
#include "BKE_blendfile.hh"
#include "BKE_image.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BKE_report.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "BLO_writefile.hh"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "IMB_colormanagement.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"
#include "MEM_guardedalloc.h"
#include <algorithm>
#include <array>
#include <clocale>
#include <future>
#include <iostream>

namespace {
using namespace bts;using namespace blender;
using Pixel=std::array<float,4>;
void require(bool b,const char *s){if(!b)throw std::runtime_error(s);}
QByteArray load(const QString &p){QFile f(p);require(f.open(QIODevice::ReadOnly),"Own fixture missing");return f.readAll();}
QByteArray sha(const QString &p){return QCryptographicHash::hash(load(p),QCryptographicHash::Sha256).toHex();}
bNodeSocket &socket(bNode &n,const char *id,bool input){for(auto &s:input?n.inputs:n.outputs)if(STREQ(s.identifier,id))return s;throw std::runtime_error("Fixture socket missing");}
Pixel transform(Pixel p,const char *from,const char *to){IMB_colormanagement_transform_v4(p.data(),from,to);return p;}
const Pixel constant{.2f,.7f,.35f,1},encoded{224/255.f,32/255.f,96/255.f,1};
void fixture(const QString &path,const QString &imagepath){
 using bke::blendfile::PartialWriteContext;
 std::unique_ptr<Main,decltype(&BKE_main_free)> main(BKE_main_new(),BKE_main_free);IMB_colormanagement_working_space_init_default(main.get());
 auto *image=BKE_image_load(main.get(),imagepath.toUtf8().constData());require(image,"Cannot add source image");STRNCPY(image->colorspace_settings.name,"sRGB");image->alpha_mode=IMA_ALPHA_STRAIGHT;image->flag|=IMA_HIGH_BITDEPTH;
 auto *material=BKE_material_add(main.get(),"Recorded-image-color");auto &tree=*material->nodetree;
 auto *tex=bke::node_add_node(nullptr,tree,UString("ShaderNodeTexImage"));tex->id=&image->id;id_us_plus(tex->id);
 auto *mix=bke::node_add_node(nullptr,tree,UString("ShaderNodeMixRGB"));mix->custom1=MA_RAMP_MULT;
 auto *fac=static_cast<bNodeSocket *>(mix->inputs.first),*a=fac->next,*b=a->next;
 static_cast<bNodeSocketValueFloat *>(fac->default_value)->value=1;
 std::copy(constant.begin(),constant.end(),static_cast<bNodeSocketValueRGBA *>(b->default_value)->value);
 auto *pbr=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled"));auto *output=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));
 bke::node_add_link(tree,*tex,socket(*tex,"Color",false),*mix,*a);
 bke::node_add_link(tree,*mix,socket(*mix,"Color",false),*pbr,socket(*pbr,"Base Color",true));
 bke::node_add_link(tree,*pbr,socket(*pbr,"BSDF",false),*output,socket(*output,"Surface",true));BKE_ntree_update_after_single_tree_change(*main,tree);
 PartialWriteContext writer(*main);writer.id_add(&material->id,{PartialWriteContext::ADD_DEPENDENCIES|PartialWriteContext::MAKE_LOCAL|PartialWriteContext::SET_FAKE_USER});
 ReportList reports{};BKE_reports_init(&reports,RPT_STORE);const auto clear=qScopeGuard([&]{BKE_reports_free(&reports);});
 require(writer.write(path.toUtf8().constData(),0,BLO_WRITE_PATH_REMAP_NONE,reports),"Cannot write own source blend");
}
struct Probe {
 BlenderHost &host;QString out,rec,source,image;QByteArray source_sha,image_sha;QJsonArray cases;
 std::uint64_t checks=0;double maximum=0;
 std::vector<std::shared_ptr<NativeMaterialGraph>> retained;
 ~Probe(){try{host.with_gpu([&]{retained.clear();});}catch(...){}}
 PreparedNativeMaterialResource prepare(bool legacy){
  return std::async(std::launch::async,[&]{
   std::atomic_bool cancel=false;std::unique_ptr<Main,decltype(&BKE_main_free)> reference(BKE_main_new(),BKE_main_free);IMB_colormanagement_working_space_init_default(reference.get());
   if(!legacy)return prepare_native_material_resource(*reference,source,"Recorded-image-color",cancel);
   auto graph=restore_native_material_graph(source,"Recorded-image-color",cancel);
   auto archive=archive_native_material(*graph,source,cancel,true);auto decoded=prepare_native_material_images(*graph,cancel);
   auto asset=native_material_asset("Recorded-image-color",std::move(archive.bytes),std::move(archive.description));
   return PreparedNativeMaterialResource{std::move(asset),std::move(graph),std::move(archive.diagnostics),std::move(archive.textures)};
  }).get();
 }
 Pixel oracle(const QString &target){
  const auto name=target.toUtf8();auto rgb=transform(encoded,"sRGB",name.constData());const auto c=transform(constant,rec.toUtf8().constData(),name.constData());
  for(int i=0;i<3;++i)rgb[i]*=c[i];return transform(rgb,name.constData(),"ACEScg");
 }
 gpu::Texture *render(const std::shared_ptr<NativeMaterialGraph> &graph,const QString &label){
  require(!graph->image_resources.empty(),"Normal worker did not bind shared source resource");
  retained.push_back(graph);
  const auto before=native_material_description_json(graph->description);gpu::Texture *published=nullptr;
  const QString target_space=QString::fromUtf8(graph->main->colorspace.scene_linear_name);const auto want=oracle(target_space);
  host.with_gpu([&]{
   NativeMaterialImageUpload upload(graph);while(!upload.step(256)){}
   auto *image=graph->image_resources.front().first;published=image->runtime->gputexture[TEXTARGET_2D][0];require(published,"No normalized GPU source");
   const auto color=std::find_if(graph->channels.begin(),graph->channels.end(),[](const auto &c){return c.channel==0;});require(color!=graph->channels.end()&&color->tree,"Missing native Color graph");
   NativeMaterialGpuProgram program(*color->tree,graph->parameters.size(),0);
   auto *target=GPU_texture_create_2d("Normal source recorded-space oracle",9,7,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr);require(target,"No oracle target");const auto free=qScopeGuard([&]{GPU_texture_free(target);});
   NativeMaterialGpuContext context;context.color_to_aces=graph->color_to_aces;program.dispatch(target,graph->parameters,context,{0,0,9,7});GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);
   auto *actual=static_cast<float *>(GPU_texture_read(target,GPU_DATA_FLOAT,0));require(actual,"No genuine GPU readback");const auto release=qScopeGuard([&]{MEM_delete(actual);});
   double error=0;for(int p=0;p<9*7;++p)for(int c=0;c<4;++c){const auto delta=std::abs(double(actual[p*4+c])-want[c]);require(std::isfinite(actual[p*4+c]),"Nonfinite native pixels");error=std::max(error,delta);++checks;}
   maximum=std::max(maximum,error);require(error<3e-5,"Mixed constant/image graph differs from recorded-space OCIO oracle");
   cases.append(QJsonObject{{"case",label},{"recorded_space",target_space},{"owner_space",QString::fromUtf8(IMB_colormanagement_working_space_get())},{"maximum_error",error},{"source_format",int(GPU_texture_format(published))}});
  });
  require(native_material_description_json(graph->description)==before,"Rendering mutated authored source interface");return published;
 }
 void run(){
  QDir().mkpath(out);image=out+"/original.png";source=out+"/original.blend";
  QImage pixels(3,2,QImage::Format_RGBA8888);pixels.fill(QColor(224,32,96));require(pixels.save(image),"Cannot create own PNG");
  std::async(std::launch::async,[&]{fixture(source,image);}).get();source_sha=sha(source);image_sha=sha(image);
  rec=QString::fromUtf8(IMB_colormanagement_working_space_get_default());const auto prior=std::string(IMB_colormanagement_working_space_get());const auto restore=qScopeGuard([&]{IMB_colormanagement_working_space_set_from_name(prior.c_str());});
  IMB_colormanagement_working_space_set_from_name(rec.toUtf8().constData());auto canonical=prepare(false);require(QString::fromUtf8(canonical.graph->main->colorspace.scene_linear_name)==u"ACEScg","Normal import not canonical AP1");auto *first=render(canonical.graph,"normal-import-under-legacy-owner");
  IMB_colormanagement_working_space_set_from_name("ACEScg");auto warm=prepare(false);auto *second=render(warm.graph,"same-normal-source-under-AP1-owner");
  require(canonical.graph->image_resources.front().second==warm.graph->image_resources.front().second&&first==second,"Equivalent target duplicated CPU/GPU image cache after owner switch");
  auto legacy=prepare(true);require(QString::fromUtf8(legacy.graph->main->colorspace.scene_linear_name)==rec,"Legacy encoded graph target silently canonicalized");auto *old=render(legacy.graph,"recorded-legacy-graph-under-AP1-owner");
  require(legacy.graph->image_resources.front().second!=canonical.graph->image_resources.front().second&&old!=first,"Different recorded graph spaces share normalized CPU/GPU images");
  // Keep all original encoded asset bytes; restore through resource bytes into
  // an empty cache after the original source path becomes unavailable.
  auto texture_bytes=legacy.textures;for(auto &asset:texture_bytes){require(asset.texture_files.size()==1,"Expected one original source");asset.bytes=load(asset.texture_files.front().path);require(QCryptographicHash::hash(asset.bytes,QCryptographicHash::Sha256).toHex()==image_sha,"Snapshot changed original encoded image");asset.texture_files.clear();asset.path.clear();}
  const auto native_bytes=QCryptographicHash::hash(legacy.asset.bytes,QCryptographicHash::Sha256);
  host.with_gpu([&]{canonical.graph.reset();warm.graph.reset();legacy.graph.reset();retained.clear();});
  require(native_material_image_pool_statistics().live_resources==0,"Cold reopen retained normalized CPU resources");
  const auto before_cold=native_material_image_pool_statistics().decodes;
  require(QFile::rename(image,image+".offline"),"Cannot hide own original fixture");const auto put_back=qScopeGuard([&]{QFile::rename(image+".offline",image);});
  qputenv("BTS_RESOURCE_CACHE",(out+"/cold-cache").toUtf8());
  auto restored=std::async(std::launch::async,[&]{std::atomic_bool cancel=false;std::unique_ptr<Main,decltype(&BKE_main_free)> reference(BKE_main_new(),BKE_main_free);IMB_colormanagement_working_space_init_default(reference.get());return hydrate_native_material_resource(*reference,legacy.asset,cancel,texture_bytes);}).get();
  require(QString::fromUtf8(restored->main->colorspace.scene_linear_name)==rec,"Cold Save/Open lost recorded legacy space");render(restored,"source-free-cold-legacy-resource-open");
  require(native_material_image_pool_statistics().decodes==before_cold+1,"Cold reopen did not decode original bytes again");
  require(QCryptographicHash::hash(legacy.asset.bytes,QCryptographicHash::Sha256)==native_bytes,"Save/Open mutated original native asset bytes");
  require(sha(image+".offline")==image_sha&&sha(source)==source_sha,"Original synthetic source bytes changed");
  QFile f(out+"/gpu.json");require(f.open(QIODevice::WriteOnly),"Cannot write GPU proof");f.write(QJsonDocument(QJsonObject{{"cases",cases},{"pixel_components",qint64(checks)},{"maximum_error",maximum},{"tolerance",3e-5},{"genuine_gpu_readback",true},{"source_free_cold_legacy_open",true},{"cold_cpu_pool_empty",true},{"native_asset_sha256",QString::fromLatin1(native_bytes.toHex())},{"original_png_sha256",QString::fromLatin1(image_sha)},{"original_blend_sha256",QString::fromLatin1(source_sha)},{"original_source_unchanged",true},{"cpu_gpu_cache_target_isolation",true}}).toJson());
  host.with_gpu([&]{restored.reset();canonical.graph.reset();warm.graph.reset();legacy.graph.reset();retained.clear();});
 }
};
}
int main(int argc,char **argv){
 QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);QSurfaceFormat fmt;fmt.setVersion(4,3);fmt.setProfile(QSurfaceFormat::CoreProfile);fmt.setDepthBufferSize(24);QSurfaceFormat::setDefaultFormat(fmt);QApplication app(argc,argv);std::setlocale(LC_NUMERIC,"C");
 try{
  require(argc==2,"Expected private output directory");BlenderHost host(QCoreApplication::applicationFilePath().toStdString(),"");
  QOpenGLContext context;context.setFormat(fmt);require(context.create(),"Cannot create private context");QOffscreenSurface surface;surface.setFormat(context.format());surface.create();require(surface.isValid()&&context.makeCurrent(&surface),"Cannot activate offscreen surface");QOpenGLFramebufferObject fbo(64,64,QOpenGLFramebufferObject::CombinedDepthStencil);require(fbo.isValid(),"No private framebuffer");const auto close=qScopeGuard([&]{host.shutdown();});host.initialize_gpu(context,surface,fbo.handle());require(QString::fromStdString(host.renderer()).contains("NVIDIA",Qt::CaseInsensitive),"Requires native NVIDIA renderer");
  Probe probe{host,QString::fromLocal8Bit(argv[1])};probe.run();std::cout<<"NATIVE_IMAGE_COLOR_GPU_PASS checks="<<probe.checks<<" max="<<probe.maximum<<'\n';return 0;
 }catch(const std::exception &e){std::cerr<<"NATIVE_IMAGE_COLOR_GPU_FAIL "<<e.what()<<'\n';return 1;}
}
