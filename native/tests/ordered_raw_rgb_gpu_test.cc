// SPDX-License-Identifier: GPL-3.0-or-later
#include "OrderedRawRgbFixture.hh"
#include "BlenderHost.hh"
#include "GpuLayerPreview.hh"
#include "FillTextureIO.hh"
#include "LayerChannelCommand.hh"
#include "AssetCatalog.hh"
#include "NativeMaterialMultiObjectDemo.hh"
#include "IMB_colormanagement.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "GPU_texture.hh"
#include "MEM_guardedalloc.h"
#include <QApplication>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>
#include <QScopeGuard>
#include <QElapsedTimer>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSet>
#include <future>

using namespace bts;
using ordered_rgb_test::require;
namespace {
void wait(const auto &ready){QElapsedTimer clock;clock.start();while(!ready()){QCoreApplication::processEvents();QThread::msleep(1);require(clock.elapsed()<120000,"Ordered RGB timed out");}}
auto await(auto future){wait([&]{return future.wait_for(std::chrono::seconds(0))==std::future_status::ready;});return future.get();}
struct Probe {
 BlenderHost &host;QString out;double maximum=0,minimum_value=0,maximum_value=0;std::uint64_t components=0,boundary_roundings=0;double maximum_literal_delta=0,maximum_float_error=0;QJsonArray cases;
 QSet<QString> completed;
 void checkpoint(){
  QSaveFile file(out+"/case-checkpoint.json");require(file.open(QIODevice::WriteOnly),"Cannot write per-case checkpoint");
  file.write(QJsonDocument(QJsonObject{{"production_sha",qEnvironmentVariable("BTS_ORDERED_PRODUCTION_SHA")},{"oracle_sha",qEnvironmentVariable("BTS_ORDERED_ORACLE_SHA")},{"cases",cases},{"components",qint64(components)},{"maximum_error",maximum},{"maximum_literal_quantized_delta",maximum_literal_delta},{"maximum_float32_analytic_error",maximum_float_error},{"rounding_boundary_components",qint64(boundary_roundings)},{"minimum_output",minimum_value},{"maximum_output",maximum_value}}).toJson());require(file.commit(),"Cannot commit case checkpoint");
 }
 void resume(){
  const auto path=qEnvironmentVariable("BTS_ORDERED_RESUME_CASES");if(path.isEmpty())return;QFile file(path);require(file.open(QIODevice::ReadOnly),"Cannot read resume checkpoint");const auto proof=QJsonDocument::fromJson(file.readAll()).object();
  require(!qEnvironmentVariable("BTS_ORDERED_PRODUCTION_SHA").isEmpty()&&proof["production_sha"]==qEnvironmentVariable("BTS_ORDERED_PRODUCTION_SHA")&&proof["oracle_sha"]==qEnvironmentVariable("BTS_ORDERED_ORACLE_SHA"),"Resume source/oracle hashes differ");
  cases=proof["cases"].toArray();for(const auto &entry:cases){const auto c=entry.toObject();completed.insert(c["key"].toString());}
  components=proof["components"].toInteger();maximum=proof["maximum_error"].toDouble();maximum_literal_delta=proof["maximum_literal_quantized_delta"].toDouble();maximum_float_error=proof["maximum_float32_analytic_error"].toDouble();boundary_roundings=proof["rounding_boundary_components"].toInteger();minimum_value=proof["minimum_output"].toDouble();maximum_value=proof["maximum_output"].toDouble();
 }
 std::shared_ptr<const LinearColorTransform> matrix,identity_matrix;
 std::array<double,3> convert(std::array<double,3> value,bool enabled=true){
  if(!enabled)return value;float pixel[4]{float(value[0]),float(value[1]),float(value[2]),1};
  blender::IMB_colormanagement_transform_v4(pixel,matrix->input_space.c_str(),"ACEScg");return {pixel[0],pixel[1],pixel[2]};
 }
 void cold_open(){
  require(!QFile::exists(out+"/geometry.blend")&&!QFile::exists(out+"/ordered-raw.exr"),"Original input still available for cold packed Open");
  QFile expected(out+"/expected-document.json");require(expected.open(QIODevice::ReadOnly),"Missing expected document metadata");const auto before=QJsonDocument::fromJson(expected.readAll()).object();
  auto document=host.open_project((out+"/ordered-project.blend").toStdString());require(document.to_json()==before,"Cold packed Open changed ordered stage metadata");
  auto &set=document.set(document.texture_sets().front().material_session_uid);const auto &source=set.layers.back().fill.texture;matrix=set.layers.back().effects.back().linear_color;
  require(source&&source->raw&&source->pixels&&source->pixels==fill_channel_texture(set.layers.back().fill,2)->pixels,"Cold Open did not share raw Color/Roughness decoding");
  for(int i=0;i<4;++i)for(int c=0;c<3;++c)require(source->pixels->at(i)[c]==ordered_rgb_test::raw_pixels[i][c],"Cold EXR decode changed literal signed/HDR input RGB");
  host.with_gpu([&]{GpuLayerPreview cache;const auto cleanup=qScopeGuard([&]{cache.reset();});check(cache,set,"cold-exr-packed-open");check(cache,set,"cold-exr-shared-roughness",2);require(cache.texture_resource_allocations()==1,"Cold Open duplicated raw Color/Roughness GPU allocation");});
  QFile result(out+"/cold-open.json");require(result.open(QIODevice::WriteOnly),"Cannot write cold Open proof");result.write(QJsonDocument(QJsonObject{{"cases",cases},{"components",qint64(components)},{"maximum_error",maximum},{"maximum_literal_quantized_delta",maximum_literal_delta},{"maximum_float32_analytic_error",maximum_float_error},{"rounding_boundary_components",qint64(boundary_roundings)},{"source_free_cold_exr_save_open",true},{"fresh_process",true},{"shared_raw_cpu_gpu_resource",true},{"exact_signed_hdr_source_rgb",true},{"interactive_cpu_composition",false}}).toJson());
 }
 void check(GpuLayerPreview &cache,TextureSet &set,const char *label,int channel=0){
  using namespace blender;
  std::cout<<"ORDERED_RGB_CHECK "<<label<<" channel="<<channel<<" precision="<<int(set.precision[channel])<<std::endl;
  const auto key=QString("%1:%2:%3").arg(label).arg(channel).arg(int(set.precision[channel]));const bool resumed=completed.contains(key);
  // Called inside exactly one owner transaction. Blender's draw-context ticket
  // mutex is deliberately nonrecursive; nesting with_gpu here would deadlock.
  {
   auto *target=GPU_texture_create_2d("Ordered RGB independent oracle",128,128,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr);require(target,"No ordered RGB target");const auto free=qScopeGuard([&]{GPU_texture_free(target);});
   double error=0;
   for(int tile:{1001,1002}){
    wait([&]{cache.begin_frame();const auto result=cache.update(set,channel,target,tile);require(result!=GpuPreviewUpdate::Unsupported,"Ordered RGB GPU rejected");return result==GpuPreviewUpdate::Updated;});
    if(resumed)continue;
    auto *stage_image=cache.diagnostic_composite_texture(set,channel,tile);require(stage_image,"Missing completed F32 stack stage");
    auto *stage=static_cast<float *>(GPU_texture_read(stage_image,GPU_DATA_FLOAT,0));require(stage,"Missing F32 stage readback");const auto release_stage=qScopeGuard([&]{MEM_delete(stage);});
    auto *pixels=static_cast<float *>(GPU_texture_read(target,GPU_DATA_FLOAT,0));require(pixels,"Ordered RGB readback missing");const auto release=qScopeGuard([&]{MEM_delete(pixels);});
    for(int y=0;y<128;++y)for(int x=0;x<128;++x){
     auto reference=ordered_rgb_test::expected(set,x,y,tile,[&](auto rgb){return convert(rgb,set.layers.back().effects.back().enabled);});
     for(int c=0;c<(channel==0?3:1);++c){
      const double expected=channel==0?ordered_rgb_test::quantize(reference[c],set.precision[0]):std::clamp(ordered_rgb_test::sample(x,y,tile)[0],0.,1.);
      if(channel==0){minimum_value=std::min(minimum_value,expected);maximum_value=std::max(maximum_value,expected);}
      const double actual=pixels[(y*128+x)*4+c],difference=std::abs(actual-expected);double oracle_error=difference;++components;
      maximum_literal_delta=std::max(maximum_literal_delta,difference);
      // Native Levels/OCIO/GPU arithmetic is float. At a discrete rounding
      // boundary a sub-micro-unit analytic difference can select either adjacent
      // encoded value. Admit only endpoints of this independently computed
      // interval; record every such component and its literal encoded delta.
      const double float_bound=1e-6*(1+std::abs(reference[c]));
      if(channel==0){
       const double captured=stage[(y*128+x)*4+c],stage_error=std::abs(captured-reference[c]);
       maximum_float_error=std::max(maximum_float_error,stage_error);
       require(stage_error<=float_bound,"F32 stage exceeds the independent arithmetic bound");
       require(std::abs(stage[(y*128+x)*4+3]-1)<1e-6,"F32 stage has unexpected coverage");
       const double encoded_from_stage=ordered_rgb_test::quantize(captured,set.precision[0]);
       require(std::abs(actual-encoded_from_stage)<=3e-5,"Encoded output does not quantize the captured F32 stage");
      }
      if(channel==0&&set.precision[0]!=ChannelPrecision::RGB32F&&difference>1e-6){
       const double low=ordered_rgb_test::quantize(reference[c]-float_bound,set.precision[0]);
       const double high=ordered_rgb_test::quantize(reference[c]+float_bound,set.precision[0]);
       const double endpoint_error=std::min(std::abs(actual-low),std::abs(actual-high));
       require(low!=high&&endpoint_error<1e-6,"Encoded bin differs away from the independent float rounding boundary");
       oracle_error=endpoint_error;++boundary_roundings;
      }
      error=std::max(error,oracle_error);
      if(oracle_error>3e-5){std::cerr<<label<<" tile="<<tile<<" x="<<x<<" y="<<y<<" c="<<c<<" actual="<<pixels[(y*128+x)*4+c]<<" expected="<<expected<<" unquantized="<<reference[c]<<" error="<<difference<<'\n';throw std::runtime_error("Ordered RGB independent GPU oracle mismatch");}
     }
     require(std::abs(pixels[(y*128+x)*4+3]-1)<1e-6,"Raw A changed authored coverage");
    }
   }
   if(resumed){std::cout<<"ORDERED_RGB_RESUMED "<<key.toStdString()<<std::endl;return;}
   maximum=std::max(maximum,error);cases.append(QJsonObject{{"key",key},{"case",label},{"channel",channel},{"precision",int(set.precision[channel])},{"maximum_error",error}});completed.insert(key);checkpoint();
  }
 }
 void run(){
  using namespace blender;using namespace ordered_rgb_test;
  const auto prior=std::string(IMB_colormanagement_working_space_get());const auto restore=qScopeGuard([&]{IMB_colormanagement_working_space_set_from_name(prior.c_str());});
  for(const char *owner:{IMB_colormanagement_working_space_get_default(),"ACEScg"}){
   IMB_colormanagement_working_space_set_from_name(owner);const auto before=std::string(IMB_colormanagement_working_space_get());
   auto prepared=await(std::async(std::launch::async,[]{return prepare_linear_color_transform(LinearColorInput::Rec709);}));
   require(before==IMB_colormanagement_working_space_get(),"Named processor changed global working space");if(matrix)require(matrix->to_aces==prepared->to_aces,"Owner role changed named matrix");matrix=prepared;
   auto identity=await(std::async(std::launch::async,[]{return prepare_linear_color_transform(LinearColorInput::ACEScg);}));for(int r=0;r<3;++r)for(int c=0;c<3;++c)require(std::abs(identity->to_aces[c*3+r]-float(c==r))<2e-6,"Named ACEScg is not identity");identity_matrix=identity;
  }
  auto doc=document();auto &set=doc.set(1);configure(set,matrix);const auto source=set.layers.back().fill.texture;const auto raw=*source->pixels;
  host.with_gpu([&]{GpuLayerPreview cache;const auto cleanup=qScopeGuard([&]{cache.reset();});
   for(auto precision:{ChannelPrecision::RGB32F,ChannelPrecision::RGB8,ChannelPrecision::SRGB8,ChannelPrecision::RGB16,ChannelPrecision::RGB16F}){set.precision[0]=precision;check(cache,set,"fused");}
   set.precision[0]=ChannelPrecision::RGB32F;set.layers.back().opacity[0]=1;set.layers.back().blending[0]=LayerBlend::Normal;set.layers.back().values.base_color={1.25f,1.5f,.75f,1};check(cache,set,"signed-hdr-fused");require(minimum_value<0&&maximum_value>1,"Signed/HDR oracle does not exercise unclamped results");
   set.layers.back().mask=LayerMask{};set.layers.back().mask->background=.375f;check(cache,set,"fused-partial-mask");set.layers.back().mask->background=0;check(cache,set,"fused-zero-mask");set.layers.back().mask.reset();
   check(cache,set,"shared-roughness",2);require(cache.texture_resource_allocations()==1,"Color/Roughness duplicated raw GPU texture");
   const ThumbnailRequest request{set.layers.back().id,0,false};wait([&]{cache.begin_frame();const auto result=cache.thumbnail(set,request,71);require(result!=GpuPreviewUpdate::Unsupported,"Source query unsupported");return result==GpuPreviewUpdate::Updated;});
   wait([&]{cache.displacement_summaries();const auto packet=cache.thumbnail_result();return packet&&packet->valid&&packet->ticket==71;});
   for(auto precision:{ChannelPrecision::RGB32F,ChannelPrecision::RGB8,ChannelPrecision::SRGB8,ChannelPrecision::RGB16,ChannelPrecision::RGB16F}){set.precision[0]=precision;check(cache,set,"staged");}
   set.precision[0]=ChannelPrecision::RGB32F;set.layers.back().mask=LayerMask{};set.layers.back().mask->background=.375f;check(cache,set,"staged-partial-mask");set.layers.back().mask->background=0;check(cache,set,"staged-zero-mask");set.layers.back().mask.reset();check(cache,set,"staged-restored-mask");
   const auto node=set.layers.back().id.toString().toStdString(),source_node=node+"/source",matrix_node=node+"/effect/"+set.layers.back().effects.back().id.toString().toStdString();
   const auto raw_revision=cache.content_node_revision(set,0,1001,source_node),matrix_revision=cache.content_node_revision(set,0,1001,matrix_node);require(raw_revision&&matrix_revision,"Staged source/matrix cache nodes absent");
   set.layers.back().values.base_color={1.125f,.5f,.8125f,1};check(cache,set,"canonical-tint-edit");require(raw_revision==cache.content_node_revision(set,0,1001,source_node),"Canonical tint invalidated raw source");require(matrix_revision<cache.content_node_revision(set,0,1001,matrix_node),"Tint failed to invalidate matrix output");
   const auto tinted_revision=cache.content_node_revision(set,0,1001,matrix_node);const auto recorded=matrix;matrix=identity_matrix;set.layers.back().effects.back().linear_color=matrix;check(cache,set,"named-matrix-edit");require(raw_revision==cache.content_node_revision(set,0,1001,source_node)&&tinted_revision<cache.content_node_revision(set,0,1001,matrix_node),"Named matrix cache dependencies are wrong");matrix=recorded;set.layers.back().effects.back().linear_color=matrix;
   set.layers.back().effects.back().enabled=false;check(cache,set,"disabled-matrix-identity");require(raw_revision==cache.content_node_revision(set,0,1001,source_node),"Matrix edit invalidated raw source");
   for(auto precision:{ChannelPrecision::RGB8,ChannelPrecision::SRGB8,ChannelPrecision::RGB16F}){set.precision[0]=precision;check(cache,set,"disabled-matrix-raw-precision");}
   set.precision[0]=ChannelPrecision::RGB32F;set.layers.back().effects.back().enabled=true;
   require(cache.texture_resource_allocations()==1&&*source->pixels==raw&&source->pixels==fill_channel_texture(set.layers.back().fill,2)->pixels,"Ordered stages copied or modified shared raw pixels");
   const auto undo=set;const auto previous=set.layers.back();require(set_layer_channel(set,previous.id,0,false),"Color channel did not clear");cache.retire_layer_channel(doc,set,previous,0);
   require(!set.layers.back().fill.texture&&!raw_linear_color_source(set.layers.back())&&cache.texture_resource_ready(*source),"Clearing Color broke shared Roughness resource");check(cache,set,"color-off-retains-shared-roughness",2);
   set=undo;check(cache,set,"color-off-undo");require(cache.texture_resource_allocations()==1,"Undo duplicated retained shared GPU image");
  });
  auto exclusive=document();auto &only=exclusive.set(1);configure(only,matrix);set_layer_channel(only,only.layers.back().id,2,false);
  host.with_gpu([&]{GpuLayerPreview cache;const auto cleanup=qScopeGuard([&]{cache.reset();});check(cache,only,"exclusive-color-source");const auto undo=only;const auto previous=only.layers.back();const auto bitmap=previous.fill.texture;set_layer_channel(only,previous.id,0,false);cache.retire_layer_channel(exclusive,only,previous,0);require(!cache.texture_resource_ready(*bitmap),"Clearing last Color binding retained GPU source");only=undo;check(cache,only,"exclusive-color-source-undo");require(cache.texture_resource_allocations()==2,"Undo did not recreate exactly the retired raw GPU source");});
  std::cout<<"ORDERED_RGB_STAGE packed-exr-save"<<std::endl;
  const auto geometry=out+"/geometry.blend";await(std::async(std::launch::async,[&]{native_object_demo::fixture(geometry); }));
  NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;options.uv_workflow=UvWorkflow::PreserveTiles;options.blend.objects={"AW_Object_A","AW_Object_B"};
  const auto imported=host.new_project(geometry.toStdString(),qEnvironmentVariable("BTS_ENVIRONMENT").toStdString(),&options);TextureDocument packed(options,imported,host.scene_snapshot());
  const auto image=out+"/ordered-raw.exr";await(std::async(std::launch::async,[&]{auto *buffer=IMB_allocImBuf(2,2,ImBufFlags::FloatData);require(buffer,"Cannot allocate own EXR");const auto release=qScopeGuard([&]{IMB_freeImBuf(buffer);});buffer->channels=4;buffer->color_mode=ImColorMode::RGBA;buffer->ftype=IMB_FTYPE_OPENEXR;buffer->flags|=ImBufFlags::AlphaChannelPacked;for(int i=0;i<4;++i){std::copy(raw_pixels[i].begin(),raw_pixels[i].end(),buffer->float_data_for_write()+i*4);buffer->float_data_for_write()[i*4+3]=float(i+1)/8;}require(IMB_save_image(buffer,image.toUtf8().constData(),ImBufFlags::FloatData),"Cannot save own EXR");}));
  QFile original(image);require(original.open(QIODevice::ReadOnly),"Cannot read own EXR bytes");StudioAsset asset;asset.id=QUuid::createUuid();asset.name="Synthetic raw RGB/Roughness";asset.filename="ordered-raw.exr";asset.bytes=original.readAll();original.close();asset.usages={"texture"};AssetCatalog catalog(out+"/catalog");catalog.add(asset);const auto id=catalog.retain_project(asset);const auto assets=catalog.project_assets();const auto retained=std::find_if(assets.begin(),assets.end(),[&](const auto &a){return a.id==id;});require(retained!=assets.end(),"Missing retained raw fixture");
  const auto decoded=await(std::async(std::launch::async,[&]{return decode_fill_texture(*retained,true);}));auto &saved=packed.set(packed.texture_sets().front().material_session_uid);configure(saved,matrix,decoded);
  const InspectionMaterial inspection{saved.material_session_uid,saved.base_values,{}};host.set_selected_material(saved.material_session_uid);host.set_inspection(InspectionMode::Material,std::span(&inspection,1));host.apply_texture_set(saved,128);wait([&]{host.flush_texture_updates();return !host.texture_updates_pending();});
  const auto before=packed.to_json();const auto file=out+"/ordered-project.blend";
  QFile expected(out+"/expected-document.json");require(expected.open(QIODevice::WriteOnly),"Cannot write expected metadata");expected.write(QJsonDocument(before).toJson());expected.close();
  host.begin_save_project(packed,file.toStdString(),catalog.project_assets());wait([&]{host.flush_texture_updates();host.poll_save_project();return !host.save_project_pending();});
  require(QFile::rename(geometry,geometry+".hidden")&&QFile::rename(image,image+".hidden"),"Cannot hide private input sources");
  QFile proof(out+"/verified.json");require(proof.open(QIODevice::WriteOnly),"Cannot write ordered RGB proof");proof.write(QJsonDocument(QJsonObject{{"cases",cases},{"components",qint64(components)},{"maximum_error",maximum},{"maximum_literal_quantized_delta",maximum_literal_delta},{"maximum_float32_analytic_error",maximum_float_error},{"rounding_boundary_components",qint64(boundary_roundings)},{"minimum_output",minimum_value},{"maximum_output",maximum_value},{"source_free_save_open",false},{"shared_raw_cpu_gpu_resource",true},{"raw_cache_survives_tint_matrix_edits",true},{"explicit_named_space_owner_independent",true},{"independent_ocio_oracle",true},{"interactive_cpu_composition",false},{"source_appearance_equivalence_claim",false}}).toJson());
 }
};
}
int main(int argc,char **argv){
 QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);QApplication app(argc,argv);
 try{require(argc==2||(argc==3&&QString::fromLocal8Bit(argv[2])=="--cold-open"),"Expected private output directory");QDir().mkpath(argv[1]);BlenderHost host(QCoreApplication::applicationFilePath().toStdString(),"");QOpenGLContext context;context.setFormat(format);require(context.create(),"Cannot create GPU context");QOffscreenSurface surface;surface.setFormat(context.format());surface.create();require(context.makeCurrent(&surface),"Cannot activate offscreen context");QOpenGLFramebufferObject framebuffer(128,128,QOpenGLFramebufferObject::CombinedDepthStencil);const auto close=qScopeGuard([&]{host.shutdown();});host.initialize_gpu(context,surface,framebuffer.handle());require(QString::fromStdString(host.renderer()).contains("NVIDIA",Qt::CaseInsensitive),"Expected native NVIDIA GPU");Probe probe{host,QString::fromLocal8Bit(argv[1])};probe.resume();if(argc==3)probe.cold_open();else probe.run();std::cout<<"ORDERED_RAW_RGB_GPU_PASS components="<<probe.components<<" error="<<probe.maximum<<'\n';return 0;}catch(const std::exception &e){std::cerr<<"ORDERED_RAW_RGB_GPU_FAIL "<<e.what()<<'\n';return 1;}
}
