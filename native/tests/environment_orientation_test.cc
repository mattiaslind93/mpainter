#include "BlenderHost.hh"
#include "OrbitCamera.hh"
#include "TextureDocument.hh"
#include "MaterialSpherePreview.hh"
#include "BKE_global.hh"
#include "BKE_world.h"
#include "DNA_world_types.h"
#include "IMB_colormanagement.hh"
#include "BLI_listbase.h"
#include "BKE_main.hh"
#include "BKE_idprop.hh"
#include "DNA_scene_types.h"
#include <QApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QThread>
#include <QElapsedTimer>
#include <cstdio>
using namespace bts;
int main(int argc,char **argv){
 try{
  if(argc!=6)throw std::runtime_error("executable mesh initial-hdr final-hdr output");
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QSurfaceFormat format;format.setRenderableType(QSurfaceFormat::OpenGL);format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);format.setDepthBufferSize(24);format.setAlphaBufferSize(8);format.setSamples(0);QSurfaceFormat::setDefaultFormat(format);
  QApplication app(argc,argv);std::setlocale(LC_NUMERIC,"C");QString out=argv[5];QDir().mkpath(out);
  BlenderHost host(argv[1],"");
  QOpenGLContext context;context.setFormat(format);if(!context.create())throw std::runtime_error("context");QOffscreenSurface surface;surface.setFormat(context.format());surface.create();context.makeCurrent(&surface);
  QOpenGLFramebufferObject target(768,768,QOpenGLFramebufferObject::CombinedDepthStencil);
  struct Close { BlenderHost &host; ~Close(){host.shutdown();} } close{host};
  host.initialize_gpu(context,surface,target.handle());
  NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;
  const auto imported=host.new_project(argv[2],argv[3],&options);host.set_environment_resource(argv[4]);host.set_environment_appearance("opacity",0);
  // Give both fixtures the same World datablock (as saved projects have).
  // Cycles changes light importance sampling when World is absent. Camera
  // color is normalized here, without touching either host's HDR transform.
  for(blender::Scene &native:blender::G.main->scenes){
    if(!native.world)native.world=blender::BKE_world_add(blender::G.main,"HDR rotation fixture");
    float rgb[3]={114.f/255,114.f/255,114.f/255};
    auto inverse=blender::ColormanageProcessor::display_processor_new(&native.view_settings,&native.display_settings,blender::DISPLAY_SPACE_DRAW,true);inverse.apply_v3(rgb);
    native.world->horr=rgb[0];native.world->horg=rgb[1];native.world->horb=rgb[2];
  }
  auto scene=host.scene_snapshot();TextureDocument document(options,imported,scene);
  std::vector<InspectionMaterial> materials;
  for(auto &m:scene.materials){MaterialValues value;value.base_color={.55,.55,.55,1};value.metallic=.8;value.roughness=.2;host.set_material_values(m.session_id,value);materials.push_back({m.session_id,value});document.set(m.session_id).base_values=value;}
  host.set_inspection(InspectionMode::Material,materials);
  OrbitCamera camera;camera.frame_bounds({scene.minimum[0],scene.minimum[1],scene.minimum[2]},{scene.maximum[0],scene.maximum[1],scene.maximum[2]},{768,768});
  const auto draw=[&]{ViewFrame f;f.pane={ViewKind::Scene,0,0,768,768};auto v=camera.view_matrix(),p=camera.projection_matrix({768,768});std::copy_n(v.constData(),16,f.view.begin());std::copy_n(p.constData(),16,f.projection.begin());auto clip=camera.clipping_range();f.clipping={clip.first,clip.second};context.makeCurrent(&surface);target.bind();host.draw(std::span(&f,1),768,768);QCoreApplication::processEvents();};
  const auto capture=[&](QString name,bool cycles=false){QElapsedTimer time;time.start();for(int i=0;i<2000;++i){draw();if(!cycles&&i>=95)break;if(cycles&&i>20&&host.render_state()["status"].toString().contains("Done",Qt::CaseInsensitive))break;if(time.elapsed()>120000)throw std::runtime_error("Render timeout");QThread::msleep(cycles?25:5);}context.makeCurrent(&surface);if(!target.toImage().save(out+"/"+name+".png"))throw std::runtime_error("capture");};
  QJsonObject proof{{"environment",host.environment_state()},{"profile",host.project_profile()},{"visible_windows",0}};
  capture("eevee");host.set_render_quality(64,false);host.set_render_preview(true,true);capture("cycles",true);proof["cycles"]=host.render_state();host.set_render_preview(false);
  host.set_environment_alignment(true);capture("world");host.set_environment_alignment(false);camera.orbit({110,55},false);capture("orbit");
  MaterialValues thumb;thumb.base_color={.55,.55,.55,1};thumb.metallic=.8;thumb.roughness=.2;material_sphere_preview(thumb,nullptr,256).save(out+"/thumbnail.png");
#ifdef MPAINTER_HDRROT
  for(const auto &set:document.texture_sets())host.apply_texture_set(set,128);
  for(int i=0;i<64;++i)draw();
  QJsonArray probes;for(int i=0;i<24;++i){double angle=(i+.37)*2*3.141592653589793/24,y=(i%3-1)*.6,r=std::sqrt(1-y*y),x=r*std::sin(angle),z=r*std::cos(angle);auto rgb=detail::preview_environment(x,y,z,0);probes.append(QJsonObject{{"direction",QJsonArray{x,y,z}},{"rgb",QJsonArray{rgb[0],rgb[1],rgb[2]}}});}proof["thumbnail_samples"]=probes;
  context.makeCurrent(&surface);target.bind();host.save_project(document,(out+"/corrected.blend").toStdString());context.makeCurrent(&surface);target.bind();document=host.open_project((out+"/corrected.blend").toStdString());for(int warm=0;warm<64;++warm)draw();proof["reopened"]=host.environment_state();if(std::abs(host.environment_state()["rotation"].toDouble()-90)>1e-4)throw std::runtime_error("saved rotation lost");
  host.set_environment_rotation(0);for(blender::Scene &s:blender::G.main->scenes){auto *props=blender::IDP_GetProperties(&s.id);if(props){auto *p=blender::IDP_GetPropertyFromGroup(props,"mpainter_environment_orientation");if(p)blender::IDP_FreeFromGroup(props,p);}}
  context.makeCurrent(&surface);target.bind();host.save_project(document,(out+"/legacy.blend").toStdString());context.makeCurrent(&surface);target.bind();document=host.open_project((out+"/legacy.blend").toStdString());for(int warm=0;warm<64;++warm)draw();proof["legacy_reopened"]=host.environment_state();
  if(std::abs(host.environment_state()["rotation"].toDouble()-90)>1e-4)throw std::runtime_error("legacy migration");
  host.set_environment_rotation(0);context.makeCurrent(&surface);target.bind();host.save_project(document,(out+"/authored-zero.blend").toStdString());context.makeCurrent(&surface);target.bind();document=host.open_project((out+"/authored-zero.blend").toStdString());for(int warm=0;warm<64;++warm)draw();proof["authored_zero_reopened"]=host.environment_state();if(host.environment_state()["rotation"].toDouble()!=0)throw std::runtime_error("authored zero lost");
  host.set_environment_rotation(37);host.set_environment_alignment(true);for(blender::Scene &s:blender::G.main->scenes){auto *props=blender::IDP_GetProperties(&s.id);if(props){auto *p=blender::IDP_GetPropertyFromGroup(props,"mpainter_environment_orientation");if(p)blender::IDP_FreeFromGroup(props,p);}}
  context.makeCurrent(&surface);target.bind();host.save_project(document,(out+"/legacy-authored.blend").toStdString());context.makeCurrent(&surface);target.bind();document=host.open_project((out+"/legacy-authored.blend").toStdString());for(int warm=0;warm<64;++warm)draw();proof["legacy_authored_reopened"]=host.environment_state();if(std::abs(host.environment_state()["rotation"].toDouble()-37)>1e-4||host.environment_state()["lighting_follows_view"].toBool())throw std::runtime_error("legacy authored angle/alignment lost");

#endif
  QFile file(out+"/proof.json");file.open(QIODevice::WriteOnly);file.write(QJsonDocument(proof).toJson());file.close();host.shutdown();return 0;
 }catch(const std::exception &e){std::fprintf(stderr,"HDRROT ERROR %s\n",e.what());return 1;}
}
