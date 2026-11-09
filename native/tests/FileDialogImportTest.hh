#pragma once
#include "BlenderHost.hh"
#include "FileDialogs.hh"
#include "OrbitCamera.hh"
#include "TextureDocument.hh"
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSettings>
#include <algorithm>
#include <stdexcept>

namespace bts::file_dialog_import_test {
inline int run(BlenderHost &host,const QString &directory,const QString &source){
 const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
 require(QGuiApplication::platformName()=="offscreen","File-dialog import test requires offscreen Qt");
 require(QFileInfo(source).isFile(),"Missing import fixture");
 QSettings::setDefaultFormat(QSettings::IniFormat);
 QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory+"/config");
 const auto environment=qEnvironmentVariable("BTS_ENVIRONMENT",QCoreApplication::applicationDirPath()+"/5.2/datafiles/studiolights/world/default-studio.hdr");
 QJsonArray imports;
 for(int iteration=0;iteration<3;++iteration){
  FileDialog::prepare_directory_thread();
  QPointer<FileDialog> dialog=new FileDialog(nullptr,"Import regression",QFileInfo(source).absolutePath());
  dialog->setFileMode(QFileDialog::ExistingFile);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->selectFile(source);
  dialog->show();QApplication::processEvents();
  require(QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection),"Cannot accept fixture");
  require(dialog&&dialog->result()==QDialog::Accepted,"Fixture selection failed");
  const auto selected=dialog->selectedFiles();
  require(selected==QStringList{source},"Unexpected selected file");
  QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
  require(!dialog,"File dialog survived deferred deletion");

  NewProjectOptions options;
  options.absolute_mesh_path=source.toStdString();
  options.resolution=DocumentResolution::Pixels128;
  // Blender's cube-base-file.abc has no UVs. Exercise the existing opt-in path.
  options.auto_unwrap.enabled=true;
  const auto summary=host.new_project(options.absolute_mesh_path,environment.toStdString(),&options);
  const auto scene=host.scene_snapshot();
  require(summary.vertices==8&&summary.polygons==6&&summary.triangles==12,"Cube geometry changed during import");
  require(scene.mesh_objects==1&&!scene.uv_triangles.empty(),"Cube surface or generated UVs missing");
  TextureDocument document(options,summary,scene);
  for(const auto &set:document.texture_sets())host.apply_texture_set(set,128);
  OrbitCamera camera;
  camera.frame_bounds({scene.minimum[0],scene.minimum[1],scene.minimum[2]},
                      {scene.maximum[0],scene.maximum[1],scene.maximum[2]},{512,512});
  ViewFrame frame;frame.pane={ViewKind::Scene,0,0,512,512};
  const auto view=camera.view_matrix(),projection=camera.projection_matrix({512,512});
  std::copy_n(view.constData(),16,frame.view.begin());
  std::copy_n(projection.constData(),16,frame.projection.begin());
  const auto clipping=camera.clipping_range();frame.clipping={clipping.first,clipping.second};
  host.draw(std::span(&frame,1),512,512);
  require(host.shader_failures().empty(),"Imported mesh shader failure");
  require(host.viewport_image().save(directory+QString("/import-%1.png").arg(iteration)),"Cannot save Metal import frame");
  const auto saved=directory+QString("/import-%1.blend").arg(iteration);
  host.save_project(document,saved.toStdString());
  const auto reopened=host.open_project(saved.toStdString());
  require(reopened.to_json()==document.to_json(),"Imported document changed on save/open");
  require(host.scene_snapshot().mesh_objects==1,"Saved mesh missing after reopen");
  imports.append(QJsonObject{{"vertices",int(summary.vertices)},{"polygons",int(summary.polygons)},
                            {"triangles",int(summary.triangles)},{"save_open",true}});
 }
 QFile output(directory+"/filedialog-import.json");
 require(output.open(QIODevice::WriteOnly),"Cannot write import proof");
 output.write(QJsonDocument(QJsonObject{{"source",source},{"renderer",QString::fromStdString(host.renderer())},
                                      {"auto_unwrap",true},{"imports",imports}}).toJson());
 return 0;
}
}
