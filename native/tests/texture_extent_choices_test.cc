// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureExtentChoices.hh"
#include "NewProjectDialog.hh"
#include "WorkspaceShell.hh"
#include "SkewPanel.hh"
#include "MeshBakeDialog.hh"
#include <QApplication>
#include <QAbstractItemModel>
#include <QDialog>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QTimer>
#include <cassert>
#include <iostream>
using namespace bts;
namespace {
int ram_queries=0;
std::uint64_t no_ram(){++ram_queries;return 0;}
QComboBox &control(QWidget &widget,const char *name){auto *box=widget.findChild<QComboBox *>(name);assert(box);return *box;}
void choices(QComboBox &box,int maximum){assert(box.findData(maximum)>=0);assert(box.findData(16384)<0);assert(box.findData(32768)<0);for(int i=0;i<box.count();++i)if(box.itemData(i).isValid())assert(box.itemData(i).toInt()<=maximum);}
}
int main(int argc,char **argv){
 QApplication app(argc,argv);assert(argc==2);const QString root=QString::fromLocal8Bit(argv[1]);QDir().mkpath(root);QFile file(root+"/mesh.obj");assert(file.open(QIODevice::WriteOnly));file.write("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");file.close();
 {
  TextureRamQueryScope ram(no_ram);texture_gpu_max_size=0;TextureExtentComboBox box(128);assert(box.findData(max_texture_resolution)>=0);assert(ram_queries==0);
  box.setCurrentIndex(box.findData(8192));texture_gpu_max_size=32768;box.showPopup();box.hidePopup();assert(box.currentData().toInt()==8192);choices(box,8192);
  texture_gpu_max_size=4096;box.showPopup();box.hidePopup();assert(box.currentData().toInt()==8192&&box.findData(32768)<0);assert(ram_queries==0);
  int mutations=0;QObject::connect(box.model(),&QAbstractItemModel::rowsInserted,&box,[&]{++mutations;});QObject::connect(box.model(),&QAbstractItemModel::rowsRemoved,&box,[&]{++mutations;});refresh_texture_extent_choices(box,128);assert(mutations==0);
 }
 SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Menu fixture";material.uniform_editable=true;scene.materials.push_back(material);ImportSummary imported;imported.materials={{1,"Menu fixture"}};TextureDocument document({},imported,scene);
 const auto resident=PaintPixels::resident_bytes();
 for(int gpu:{16384,32768,0}){
  texture_gpu_max_size=gpu;const int maximum=max_texture_resolution;
  {
   NewProjectDialog dialog;auto &box=control(dialog,"documentResolution");choices(box,maximum);box.setCurrentIndex(box.findData(maximum));dialog.findChild<QLineEdit *>("meshPath")->setText(root+"/mesh.obj");dialog.done(QDialog::Accepted);assert(dialog.options()&&int(dialog.options()->resolution)==maximum);
  }
  {
   WorkspaceShell shell(new QWidget);choices(control(shell,"textureSetResolution"),maximum);choices(control(shell,"textureSetHeight"),maximum);choices(control(shell,"uvTileResolution"),maximum);
   auto &set=document.set(1);set.texture_resolution=65536;set.texture_height=32768;set.size_linked=false;shell.show_texture_set(&set,&document.settings());assert(control(shell,"textureSetResolution").currentData().toInt()==8192);assert(control(shell,"textureSetHeight").currentData().toInt()==8192);
  }
  {
   TextureDocument::set_dimensions(document.set(1),8192,8192,true);
   SkewPanel panel;choices(control(panel,"autoBakeWidth"),maximum);choices(control(panel,"autoBakeHeight"),maximum);panel.synchronize(QJsonObject{{"resolution",65536},{"height",32768},{"locked",false}},SkewSettings{},false,false);assert(control(panel,"autoBakeWidth").currentData().toInt()==8192);assert(control(panel,"autoBakeHeight").currentData().toInt()==8192);panel.findChild<QCheckBox *>("autoBakeSizeLocked")->setChecked(true);assert(control(panel,"autoBakeHeight").currentData().toInt()==8192);
  }
  bool checked=false;QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());assert(dialog&&dialog->objectName()=="meshBakeDialog");choices(control(*dialog,"bakeResolution"),maximum);choices(control(*dialog,"bakeHeight"),maximum);checked=true;dialog->reject();});assert(!choose_mesh_bake(nullptr,document,scene));assert(checked);
  apply_mesh_bake_template(document.id(),QJsonObject{{"resolution",65536},{"height",32768},{"locked",false}});
  QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());assert(dialog);assert(control(*dialog,"bakeResolution").currentData().toInt()==8192);assert(control(*dialog,"bakeHeight").currentData().toInt()==8192);dialog->findChild<QCheckBox *>("bakeDimensionsLocked")->setChecked(true);assert(control(*dialog,"bakeHeight").currentData().toInt()==8192);dialog->reject();});assert(!choose_mesh_bake(nullptr,document,scene));
  apply_mesh_bake_template(document.id(),QJsonObject{{"resolution",512},{"height",512},{"locked",true}});
  assert(PaintPixels::resident_bytes()==resident);
 }
 texture_gpu_max_size=0;std::cout<<"Texture extent menus: actual New Project, Workspace, Baking and Skew widgets; 8K cap with GPU 16K/32K/unknown, oversized selections clamped, popup refresh and no paint allocation PASS\n";
}
