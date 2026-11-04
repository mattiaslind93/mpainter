#pragma once
#include "BakeSamplesDialogTest.hh"
#include "GpuBakeSamplesValidation.hh"
#include "BlenderHost.hh"
#include "OrbitCamera.hh"
#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QThread>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <cstdio>
namespace bts {
inline int run_bl_bake_samples_validation(BlenderHost &host,const QString &directory){
 using namespace bl_widget_test;QDir().mkpath(directory);
 auto *owner_context=QOpenGLContext::currentContext();require(owner_context&&owner_context->surface(),"BL diagnostic has no current Qt owner surface");auto *owner_surface=owner_context->surface();QOpenGLFramebufferObject owner_target(1000,700,QOpenGLFramebufferObject::CombinedDepthStencil);require(owner_target.isValid(),"Cannot allocate private BL draw target");
 // A real owned OBJ/material supplies the IDs saved by the ordinary project
 // writer. Ray-kernel geometry is independently defined in BakeSamplesFixture.
 const auto mesh=directory+"/bl-controls.obj",mtl=directory+"/bl-controls.mtl";
 {QFile file(mtl);require(file.open(QIODevice::WriteOnly),"Cannot create owned BL material");file.write("newmtl BLValidation\nKd 0.5 0.5 0.5\n");}
 {QFile file(mesh);require(file.open(QIODevice::WriteOnly),"Cannot create owned BL mesh");file.write("mtllib bl-controls.mtl\no BLValidation\nusemtl BLValidation\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0.05 0.05\nvt 0.95 0.05\nvt 0.95 0.95\nvt 0.05 0.95\nf 1/1 2/2 3/3 4/4\n");}
 const auto environment=qEnvironmentVariable("BTS_ENVIRONMENT");require(QFileInfo(environment).isFile(),"Approved BL environment is absent");
 NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;options.absolute_mesh_path=mesh.toStdString();const auto imported=host.new_project(mesh.toStdString(),environment.toStdString(),&options);const auto scene=host.scene_snapshot();require(imported.materials.size()==1&&scene.materials.size()==1,"BL controls fixture must have one material");
 TextureDocument document(options,imported,scene);const auto set_id=document.texture_sets().front().id,document_id=document.id();
 const auto skew_proof=skew(directory),dialog_proof=dialog(document,scene,directory);const auto recipe=document.bake_configuration();require(recipe["rays"].toInt()==512&&recipe["sampling"].toInt()==8,"Controls did not publish final512/AA8");
 // Match normal New/ManyUdimProjectDemo: create the inspection/paint binding
 // before serializing a uniform-editable document. Save/Open's strict node
 // validation must stay intact. Progress real ordinary frames, not flush-only.
 const auto &set=document.texture_sets().front();host.set_selected_material(set.material_session_uid);host.apply_texture_set(set,128);
 OrbitCamera camera;require(camera.frame_bounds({scene.minimum[0],scene.minimum[1],scene.minimum[2]},{scene.maximum[0],scene.maximum[1],scene.maximum[2]},{1000,700}),"Cannot frame BL diagnostic fixture");ViewFrame frame;frame.pane={ViewKind::Scene,0,0,1000,700};const auto view=camera.view_matrix(),projection=camera.projection_matrix({1000,700});std::copy_n(view.constData(),16,frame.view.begin());std::copy_n(projection.constData(),16,frame.projection.begin());const auto clip=camera.clipping_range();frame.clipping={clip.first,clip.second};
 const auto advance=[&]{int frames=0;QElapsedTimer clock;clock.start();do{require(clock.elapsed()<120000,"BL normal frame publication timed out");require(owner_context->makeCurrent(owner_surface)&&owner_target.bind(),"Cannot restore BL Qt owner draw target");host.draw(std::span<const ViewFrame>(&frame,1),1000,700);host.flush_texture_updates();QApplication::processEvents();++frames;if(host.texture_updates_pending())QThread::msleep(1);}while(frames<8||host.texture_updates_pending());return frames;};
 const int before_save_frames=advance();require(!host.texture_updates_pending(),"BL cannot Save while GPU publication is pending");
 const auto packed=directory+"/bl-baking-config.blend";host.save_project(document,packed.toStdString());auto reopened=host.open_project(packed.toStdString());
 require(reopened.id()==document_id&&reopened.texture_sets().front().id==set_id&&reopened.bake_configuration()==recipe,"Actual packed Save/Open changed baking configuration or persistent IDs");opened_dialog(reopened,host.scene_snapshot(),directory);host.set_selected_material(reopened.texture_sets().front().material_session_uid);const int after_open_frames=advance();
 QJsonObject rays;host.with_gpu([&]{rays=verify_bl_gpu_bake_samples();});
 const QJsonObject proof{{"renderer",QString::fromStdString(host.renderer())},{"scope","Actual offscreen controls and native packed configuration; explicit diagnostic ray-kernel CPU oracle only, no interactive CPU composition or job throughput claim"},{"skew",skew_proof},{"dialog",dialog_proof},{"packed_path",packed},{"packed_Open_exact",true},{"normal_binding_initialized",true},{"before_save_frames",before_save_frames},{"after_open_frames",after_open_frames},{"document_id",document_id.toString()},{"set_id",set_id.toString()},{"configuration",recipe},{"gpu",rays}};
 QFile output(directory+"/bl-baking-samples.json");require(output.open(QIODevice::WriteOnly),"Cannot write BL native proof");output.write(QJsonDocument(proof).toJson());std::puts("BTS_BL_BAKE_SAMPLES_PASS actual_widgets=2 native_packed_Open=1 GPU512=1");return 0;
}
}
