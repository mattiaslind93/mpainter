// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialFragmentBumpTest.hh"
#include "NativeMaterialFragmentGpuTest.hh"
#include "SmartMaskNativeProof.hh"
#include "NativeMaterialProjectionColdProof.hh"
#include "GpuPerformance.hh"
#include "NativeMaterialHostQueueDemo.hh"
#include "ViewportHeadlessDemo.hh"
#include "NativeMaterialStackTest.hh"
#include "MeshBakeCancelTest.hh"
#include "MaskGeneratorGpuTest.hh"
#include "BlenderHost.hh"
#include "OrbitCamera.hh"
#include "SymmetryIntersection.hh"
#include <QOpenGLPaintDevice>
#include <QPainter>
#include <QVector4D>
#include "Performance.hh"
#include "BKE_image.hh"
#include "BLI_listbase.h"
#include "BKE_image_residency.hh"
#include "BKE_main.hh"
#include "DNA_image_types.h"
#include "IMB_imbuf_types.hh"
#include "GPU_texture.hh"
#include "DRW_engine.hh"
#include "MEM_guardedalloc.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QTimer>
#include <QEventLoop>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "LargeMeshBudgetDemo.hh"
#include "BakeMemoryDemo.hh"
#include "GpuBakeRayDemo.hh"
#include "GpuNeighborhoodFilterTest.hh"
#include "GpuAdjustmentPaintTest.hh"
#include "UnloadResidencyTest.hh"
#include "AdjustmentPaintColdProof.hh"
#include "../tests/HostedShaderWorkersTest.hh"
#include "../tests/HostedGpuPassReadinessTest.hh"
#include "../tests/HostedEeveeGeneratedSourceTest.hh"
#include "../tests/HostedEeveeImageLifetimeTest.hh"
namespace bts {
#if defined(BTS_MATERIAL_LAYER_SOURCE_GPU_TEST)
int run_material_layering_sources_gpu_test(BlenderHost &,const QString &,const QString &);
#endif
using namespace blender;
namespace {
void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
float checker(int tile,int x,int y,int channel){if(channel==3)return 1;if(channel==2)return tile==1001?.25f:.75f;return (((x/64+y/64+(tile-1001))&1)==channel)?1.f:0.f;}
QJsonObject check_mips(){
 auto *main=BKE_main_new();
 struct Release{Main *main;~Release(){BKE_main_free(main);}} release{main};
 const float empty[4]={0,0,0,1};
 auto *image=BKE_image_add_generated(main,4096,4096,"BTS checker",32,1,IMA_GENTYPE_BLANK,empty,false,true,true);
 require(image,"Headless UDIM image allocation failed");image->alpha_mode=IMA_ALPHA_CHANNEL_PACKED;
 auto *second=BKE_image_add_tile(image,1002,nullptr);require(second,"UDIM tile allocation failed");second->gen_x=second->gen_y=4096;second->gen_depth=32;second->gen_type=IMA_GENTYPE_BLANK;second->gen_flag|=IMA_GEN_FLOAT;std::copy_n(empty,4,second->gen_color);require(BKE_image_fill_tile(image,second),"UDIM buffer allocation failed");
 for(int tile:{1001,1002}){ImageUser user{};user.tile=tile;auto *buffer=BKE_image_acquire_ibuf(image,&user,nullptr);require(buffer&&buffer->float_data(),"UDIM buffer missing");auto *pixels=buffer->float_data_for_write();for(int y=0;y<4096;++y)for(int x=0;x<4096;++x)for(int c=0;c<4;++c)pixels[(std::size_t(y)*4096+x)*4+c]=checker(tile,x,y,c);BKE_image_release_ibuf(image,buffer,nullptr);}
 BKE_image_gpu_set_residency_limit(image,4096);BKE_image_gpu_set_sampling_limit(image,4096);
 auto pair=BKE_image_get_gpu_material_texture(image,nullptr,true);auto *texture=*pair.texture;require(texture&&GPU_texture_width(texture)==4096,"Full source resolution not resident");
 const auto before=gpu_transfer_snapshot();
 for(int cap:{2048,4096,1024,4096}){BKE_image_gpu_set_sampling_limit(image,cap);require(*BKE_image_get_gpu_material_texture(image,nullptr,true).texture==texture,"Mip selection replaced native texture");}
 const auto after=gpu_transfer_snapshot();require(before["upload"]==after["upload"]&&before["mipmap"]==after["mipmap"],"Mip selection reuploaded/regenerated image");
 const auto *read=static_cast<const float *>(GPU_texture_read(texture,GPU_DATA_FLOAT,0));require(read,"GPU checker readback failed");double error=0;std::uint64_t count=0;
 for(const ImageTile &tile:image->tiles){const auto &r=tile.runtime;require(r.tilearray_size[0]==4096&&r.tilearray_size[1]==4096,"UDIM size changed");for(int y=0;y<4096;++y)for(int x=0;x<4096;++x)for(int c=0;c<4;++c){const auto index=((std::size_t(r.tilearray_layer)*4096+y+r.tilearray_offset[1])*4096+x+r.tilearray_offset[0])*4+c;const auto delta=read[index]-checker(tile.tile_number,x,y,c);error+=double(delta)*delta;++count;}}
 MEM_delete_void(static_cast<void *>(const_cast<float *>(read)));require(error==0,"Native full-res checker differs from source");
 // A constrained allocation exercises the GPU resample path (2D array).
 BKE_image_gpu_set_residency_limit(image,1024);BKE_image_gpu_set_sampling_limit(image,1024);auto *small=*BKE_image_get_gpu_material_texture(image,nullptr,true).texture;require(small&&GPU_texture_width(small)==1024,"GPU resample cap failed");
 const auto *low=static_cast<const float *>(GPU_texture_read(small,GPU_DATA_FLOAT,0));require(low,"GPU resample readback failed");double low_error=0;
 for(const ImageTile &tile:image->tiles){const auto &r=tile.runtime;for(int y=0;y<1024;++y)for(int x=0;x<1024;++x)for(int c=0;c<4;++c){const auto index=((std::size_t(r.tilearray_layer)*1024+y+r.tilearray_offset[1])*1024+x+r.tilearray_offset[0])*4+c;low_error=std::max(low_error,std::abs(double(low[index]-checker(tile.tile_number,x*4,y*4,c))));}}
 MEM_delete_void(static_cast<void *>(const_cast<float *>(low)));require(low_error<1e-6,"GPU low mip differs from box reference");
 // The ordinary 2D branch uses the same GPU resampler.
 auto *single=BKE_image_add_generated(main,256,128,"BTS single checker",32,1,IMA_GENTYPE_BLANK,empty,false,true,false);require(single,"2D fixture allocation failed");BKE_image_gpu_set_residency_limit(single,64);auto *single_gpu=BKE_image_get_gpu_texture(single,nullptr);require(single_gpu&&GPU_texture_width(single_gpu)==64&&GPU_texture_height(single_gpu)==32,"2D resample aspect ratio changed");
 return {{"tiles",2},{"source_resolution",4096},{"full_cap",4096},{"mse",error/count},{"psnr","infinite (pixel-exact)"},{"zoom_uploads",0},{"zoom_mipmap_rebuilds",0},{"low_cap",1024},{"low_max_error",low_error},{"single_aspect_preserved",true}};
}
}
int run_viewport_headless_demo(BlenderHost &host,const QString &directory,const QString &project){
 if(qEnvironmentVariableIsSet("BTS_ESC_CANCEL_TEST"))return run_mesh_bake_cancel_test(directory);
 QDir().mkpath(directory);QOpenGLContext context;context.setFormat(QSurfaceFormat::defaultFormat());require(context.create(),"Cannot create offscreen OpenGL context");QOffscreenSurface surface;surface.setFormat(context.format());surface.create();require(surface.isValid()&&context.makeCurrent(&surface),"Cannot activate offscreen OpenGL surface");QOpenGLFramebufferObject target(1000,700,QOpenGLFramebufferObject::CombinedDepthStencil);require(target.isValid(),"Cannot allocate offscreen Qt framebuffer");
 struct Close{BlenderHost &host;~Close(){host.shutdown();}} close{host};host.initialize_gpu(context,surface,target.handle());
 if(qEnvironmentVariableIsSet("BTS_MATERIAL_FILTER_TEST")){
  QJsonObject proof;host.with_gpu([&]{proof=verify_native_material_stack();});proof["renderer"]=QString::fromStdString(host.renderer());
  QFile file(directory+"/material-filter.json");require(file.open(QIODevice::WriteOnly),"Cannot write material filter proof");file.write(QJsonDocument(proof).toJson());return 0;
 }
 if(qEnvironmentVariableIsSet("BTS_UNLOAD_REGRESSION")){QJsonObject proof;host.with_gpu([&]{proof=unload_test::run();});QFile file(directory+"/unload-regression.json");if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write unload proof");file.write(QJsonDocument(proof).toJson());return 0;}
 if(qEnvironmentVariableIsSet("BTS_NATIVE_FRAGMENT_NORMAL_TEST")){
  // The prepared graph is created on its worker before entering GPU ownership.
  const auto fixtures=prepare_native_fragment_gpu_fixtures();QJsonObject proof;std::string failure;
  host.with_gpu([&]{try{proof=verify_native_fragment_normal_gpu(fixtures);}catch(const std::exception &error){failure=error.what();proof={{"complete",false},{"failure",QString::fromStdString(failure)}};}});
  QFile output(directory+"/fragment-normal.json");if(!output.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write prepared fragment Normal proof");output.write(QJsonDocument(proof).toJson());
  std::printf("BTS_NATIVE_FRAGMENT_NORMAL_%s output=%s error=%s\n",failure.empty()?"PASS":"FAILED",qPrintable(directory),failure.c_str());return failure.empty()?0:1;
 }
 if(qEnvironmentVariableIsSet("BTS_NATIVE_FRAGMENT_BUMP_TEST")){QJsonObject proof;host.with_gpu([&]{proof=verify_native_fragment_bump(directory);});QFile output(directory+"/fragment-bump.json");if(!output.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write fragment Bump proof");output.write(QJsonDocument(proof).toJson());return 0;}
 if(qEnvironmentVariableIsSet("BTS_BJ_PROJECTION_DATA_COLD"))return run_native_projection_cold_proof(host,directory);
 if(qEnvironmentVariableIsSet("BTS_SMART_MASK_COLD_OPEN"))return run_smart_mask_cold_open_proof(host,project,directory);
 if(qEnvironmentVariableIsSet("BTS_BM_ADJUSTMENT_COLD"))return run_adjustment_paint_cold_proof(host,directory);
 if(qEnvironmentVariableIsSet("BTS_BM_GPU_ADJUSTMENT_TEST")){
  QJsonObject proof;std::string failure;
  host.with_gpu([&]{try{proof=gpu_adjustment_test::run();}
   catch(const std::exception &error){failure=error.what();}
   catch(...){failure="Unknown BM GPU fixture exception";}
   if(!failure.empty()){proof={{"passed",false},{"failure",QString::fromStdString(failure)},{"scope","Original diagnostic exception recorded inside the live GPU owner scope; no numerical assertion bypass"}};std::fprintf(stderr,"BTS_BM_GPU_ADJUSTMENT_FAILED error=%s\n",failure.c_str());std::fflush(stderr);}
  });
  QFile output(directory+"/bm-adjustment-gpu.json");if(!output.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write BM GPU proof");output.write(QJsonDocument(proof).toJson());return failure.empty()?0:1;
 }
 if(qEnvironmentVariableIsSet("BTS_GPU_NEIGHBORHOOD_FILTER_TEST")){QJsonObject proof;host.with_gpu([&]{proof=gpu_neighborhood_test::run();});QFile output(directory+"/neighborhood-filters.json");if(!output.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write neighborhood GPU proof");output.write(QJsonDocument(proof).toJson());return 0;}
 if(qEnvironmentVariableIsSet("BTS_VERIFY_HOSTED_EEVEE_IMAGE_LIFETIME"))return hosted_eevee_image_test::run(host,directory);
 if(qEnvironmentVariableIsSet("BTS_VERIFY_HOSTED_EEVEE_GENERATED_SOURCE"))return hosted_eevee_generated_test::run(host,directory);
 if(qEnvironmentVariableIsSet("BTS_VERIFY_HOSTED_GPUPASS_READINESS"))return hosted_gpupass_test::run(host,directory);
 if(qEnvironmentVariableIsSet("BTS_VERIFY_HOSTED_SHADER_WORKERS"))return test_hosted_shader_workers(host,directory);
#if defined(BTS_MATERIAL_LAYER_SOURCE_GPU_TEST)
 if(qEnvironmentVariableIsSet("BTS_MATERIAL_LAYER_SOURCE_GPU_TEST"))return run_material_layering_sources_gpu_test(host,directory,project);
#endif
 if(qEnvironmentVariableIsSet("MPAINTER_MASK_GENERATOR_TEST"))return verify_mask_generator_thumbnails(host,directory);
 if(qEnvironmentVariableIsSet("BTS_GPU_BAKE_BVH_TEST"))return run_gpu_bake_ray_demo(host,directory);if(qEnvironmentVariableIsSet("BTS_BAKE_MEMORY_STRESS"))return run_bake_memory_demo(host,directory);if(qEnvironmentVariableIsSet("BTS_LARGE_MESH_TEST"))return verify_large_mesh_budget(host,context,surface,target,directory);if(qEnvironmentVariableIsSet("BTS_NATIVE_MATERIAL_GPU_TEST")||qEnvironmentVariableIsSet("BTS_NATIVE_THUMBNAIL_TEST")){if(qEnvironmentVariableIsSet("BTS_NATIVE_MATERIAL_HOST_QUEUE_TEST")){auto snapshot=host.scene_snapshot();OrbitCamera camera;camera.frame_bounds({snapshot.minimum[0],snapshot.minimum[1],snapshot.minimum[2]},{snapshot.maximum[0],snapshot.maximum[1],snapshot.maximum[2]},{1000,700});std::optional<std::uint32_t> framed_material;const auto frame=[&]{if(qEnvironmentVariableIsSet("BTS_MATERIAL_IMPORT_CASES")&&host.selected_material()!=framed_material){framed_material=host.selected_material();const auto current=host.scene_snapshot();if(current.has_bounds)camera.frame_bounds({current.minimum[0],current.minimum[1],current.minimum[2]},{current.maximum[0],current.maximum[1],current.maximum[2]},{1000,700});}ViewFrame f;f.pane={ViewKind::Scene,0,0,1000,700};const auto view=camera.view_matrix(),projection=camera.projection_matrix({1000,700});std::copy_n(view.constData(),16,f.view.begin());std::copy_n(projection.constData(),16,f.projection.begin());const auto clip=camera.clipping_range();f.clipping={clip.first,clip.second};context.makeCurrent(&surface);target.bind();host.draw(std::span<const ViewFrame>(&f,1),1000,700);};const auto proof=verify_native_host_material_queue(host,frame,[&]{context.makeCurrent(&surface);GLint previous=0,texture=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous);glBindFramebuffer(GL_READ_FRAMEBUFFER,target.handle());glReadBuffer(GL_COLOR_ATTACHMENT0);glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&texture);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);QImage image(1000,700,QImage::Format_RGBA8888);glReadPixels(0,0,1000,700,GL_RGBA,GL_UNSIGNED_BYTE,image.bits());std::printf("MPAINTER_FRAME_CAPTURE fbo=%u texture=%d status=%u error=%u\n",target.handle(),texture,unsigned(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)),unsigned(glGetError()));std::fflush(stdout);glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(previous));glPixelStorei(GL_PACK_ALIGNMENT,4);return image.mirrored(false,true);});QFile output(directory+"/native-host-queue.json");if(!output.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write native host queue proof");output.write(QJsonDocument(proof).toJson());}return 0;}QJsonObject result{{"renderer",QString::fromStdString(host.renderer())},{"visible_windows",0},{"pid",qint64(QCoreApplication::applicationPid())}};
 const auto load=performance::Clock::now();std::optional<TextureDocument> document;if(!project.isEmpty()){document.emplace(host.open_project(project.toStdString()));for(const auto &set:document->texture_sets())if(!set.disabled){host.set_selected_material(set.material_session_uid);break;}}result["open_ms"]=performance::ms(load);
 auto scene=host.scene_snapshot();OrbitCamera camera;camera.frame_bounds({scene.minimum[0],scene.minimum[1],scene.minimum[2]},{scene.maximum[0],scene.maximum[1],scene.maximum[2]},{1000,700});
 const auto frame=[&]{ViewFrame value;value.pane={ViewKind::Scene,0,0,1000,700};const auto view=camera.view_matrix(),projection=camera.projection_matrix({1000,700});std::copy_n(view.constData(),16,value.view.begin());std::copy_n(projection.constData(),16,value.projection.begin());const auto clip=camera.clipping_range();value.clipping={clip.first,clip.second};return host.draw(std::span(&value,1),1000,700);};
 SymmetryIntersectionCache cuts;const auto &lines=cuts.get(scene.uv_triangles,0,0);const auto overlay=[&]{const auto begin=performance::Clock::now();context.makeCurrent(&surface);target.bind();QOpenGLPaintDevice device(1000,700);QPainter painter(&device);painter.setPen(QPen(QColor(224,70,70,220),1.2));const auto view=camera.view_matrix(),matrix=camera.projection_matrix({1000,700})*view;const auto eye=view.inverted().column(3).toVector3D();QVector<QLineF> screen;screen.reserve(lines.size());const auto point=[&](QVector3D p){const auto clip=matrix*QVector4D(p,1);return QPointF((clip.x()/clip.w()+1)*500,(1-clip.y()/clip.w())*350);};for(const auto &line:lines)if(QVector3D::dotProduct(line.normal,eye-line.origin)>0)screen.append(QLineF(point(line.a),point(line.b)));painter.drawLines(screen);painter.end();return performance::ms(begin);};
 QJsonArray times,overlays;for(int i=0;i<8;++i){times.append(frame());overlays.append(overlay());}result["frame_ms"]=times;result["symmetry_overlay_ms"]=overlays;result["symmetry_segments"]=int(lines.size());result["residency_before_zoom"]=host.project_profile()["texture_residency"];const auto transfers=gpu_transfer_snapshot();camera.zoom_wheel(120);frame();camera.zoom_wheel(-120);frame();result["residency_after_zoom"]=host.project_profile()["texture_residency"];result["zoom_upload_delta"]=gpu_transfer_snapshot()["upload"].toObject()["calls"].toInteger()-transfers["upload"].toObject()["calls"].toInteger();
 DRW_gpu_context_enable_ex(false);try{result["checker"]=check_mips();DRW_gpu_context_disable_ex(false);}catch(...){DRW_gpu_context_disable_ex(false);throw;}
 QEventLoop idle;const auto wall=performance::Clock::now();const double cpu=performance::thread_ms();QTimer::singleShot(3000,&idle,&QEventLoop::quit);idle.exec();result["idle_scope"]="offscreen host event loop, no QWidget presentation";result["idle_ui_cpu_percent"]=100*(performance::thread_ms()-cpu)/performance::ms(wall);result["performance"]=performance::snapshot();
 QFile file(directory+"/headless.json");require(file.open(QIODevice::WriteOnly),"Cannot write headless proof");file.write(QJsonDocument(result).toJson());std::printf("BTS_HEADLESS_VIEWPORT_PASS renderer=%s output=%s\n",host.renderer().c_str(),qPrintable(directory));return 0;
}
}
