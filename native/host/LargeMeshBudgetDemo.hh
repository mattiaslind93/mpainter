#include <QCoreApplication>
#pragma once
#include "ResourceBudget.hh"
#include <QElapsedTimer>
#include <QJsonArray>
#if defined(__linux__)
#include <sys/resource.h>
#endif
namespace bts {
inline int verify_large_mesh_budget(BlenderHost &host,QOpenGLContext &context,QOffscreenSurface &surface,QOpenGLFramebufferObject &target,const QString &directory){
 QElapsedTimer clock;clock.start();NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;
 const auto source=qEnvironmentVariable("BTS_LARGE_MESH_TEST");const auto environment=qEnvironmentVariable("BTS_ENVIRONMENT",QCoreApplication::applicationDirPath()+"/5.2/datafiles/studiolights/world/studio.exr");
 const auto imported=host.new_project(source.toStdString(),environment.toStdString(),&options);const double import_ms=clock.nsecsElapsed()/1e6;std::printf("BTS_LARGE_MESH_STAGE stage=new_project ms=%.3f\n",import_ms);clock.restart();
 auto scene=host.scene_snapshot();TextureDocument document(options,imported,scene);const double document_ms=clock.nsecsElapsed()/1e6;std::printf("BTS_LARGE_MESH_STAGE stage=snapshot_document ms=%.3f\n",document_ms);
 if(scene.uv_triangles.size()<5000000||scene.materials.empty())throw std::runtime_error("Large mesh did not reach New project/UV snapshot");
 clock.restart();host.set_selected_material(scene.materials.front().session_id);std::printf("BTS_LARGE_MESH_STAGE stage=select_material ms=%.3f\n",clock.nsecsElapsed()/1e6);OrbitCamera camera;camera.frame_bounds({scene.minimum[0],scene.minimum[1],scene.minimum[2]},{scene.maximum[0],scene.maximum[1],scene.maximum[2]},{1000,700});
 QJsonArray frames;for(auto kind:{ViewKind::Scene,ViewKind::Uv}){clock.restart();ViewFrame frame;frame.pane={kind,0,0,1000,700};const auto view=camera.view_matrix(),projection=camera.projection_matrix({1000,700});std::copy_n(view.constData(),16,frame.view.begin());std::copy_n(projection.constData(),16,frame.projection.begin());const auto clip=camera.clipping_range();frame.clipping={clip.first,clip.second};context.makeCurrent(&surface);target.bind();host.draw(std::span(&frame,1),1000,700);frames.append(clock.nsecsElapsed()/1e6);target.toImage().save(directory+(kind==ViewKind::Scene?"/bc-viewport.png":"/bc-uv.png"));}
 const auto uv=host.uv_preview_summary();if(uv.uv_triangles<5000000)throw std::runtime_error("Large mesh UV view lost source triangles");
 qint64 rss=0;
#if defined(__linux__)
 rusage usage{};getrusage(RUSAGE_SELF,&usage);rss=usage.ru_maxrss*1024LL;
#endif
 QJsonObject proof{{"source",source},{"import_ms",import_ms},{"snapshot_document_ms",document_ms},{"triangles",qint64(scene.uv_triangles.size())},{"uv_triangles",qint64(uv.uv_triangles)},{"texture_sets",int(document.texture_sets().size())},{"first_scene_and_uv_frame_ms",frames},{"peak_rss_bytes",rss},{"available_ram_after",qint64(texture_available_ram_bytes())},{"visible_windows",0}};
 QFile output(directory+"/bc-large-mesh.json");if(!output.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write large mesh proof");output.write(QJsonDocument(proof).toJson());std::printf("BTS_LARGE_MESH_PASS triangles=%zu import_ms=%.3f peak_rss=%lld\n",scene.uv_triangles.size(),import_ms,static_cast<long long>(rss));return 0;
}
}
