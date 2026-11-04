#pragma once
#include "BakeSamplesWidgetTest.hh"
#include "MeshBakeDialog.hh"
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QTimer>
#include <exception>
namespace bts::bl_widget_test {
// Explicit diagnostic only. Exercise the production modal dialog and public
// template cache; do not replace any bake worker, widget or signal handler.
inline QJsonObject dialog(TextureDocument &document,const SceneSnapshot &scene,const QString &output){
 QJsonArray cases;QJsonObject saved;
 const auto visit=[&](int rays,int aa,bool cancel,bool reopening){
  std::exception_ptr error;bool visited=false;
  QTimer::singleShot(0,[&]{auto *settings=qobject_cast<QDialog *>(QApplication::activeModalWidget());try{
   require(settings&&settings->objectName()=="meshBakeDialog","Actual MeshBakeDialog did not open");visited=true;settings->resize(1024,768);QApplication::processEvents();
   auto &count=control<QSpinBox>(*settings,"bakeSecondaryRays");auto &preset=control<QComboBox>(*settings,"bakeSecondaryRaysPreset");auto &sampling=control<QComboBox>(*settings,"bakeAntialiasing");
   require(count.minimum()==1&&count.maximum()==512,"MeshBakeDialog does not accept1..512");
   if(!saved.isEmpty())require(count.value()==saved["rays"].toInt()&&sampling.currentData().toInt()==saved["sampling"].toInt(),"Reopened modal controls differ from saved settings");
   if(reopening)require(count.value()==512&&sampling.currentData().toInt()==8,"Opened MeshBakeDialog lost persisted512/AA8");
   for(int p:{16,32,64,128,256,512})require(preset.findData(p)>=0,"MeshBakeDialog preset missing");
   if(preset.findData(rays)>=0)select(preset,rays);else{type(count,rays);require(preset.currentData().toInt()==-1,"Exact dialog value was not Custom");}
   select(sampling,aa);require(count.value()==rays,"Dialog changed the selected ray count");
   auto &resolution=control<QComboBox>(*settings,"bakeResolution");reveal(count);const int output_y=resolution.mapTo(settings,QPoint()).y(),aa_y=sampling.mapTo(settings,QPoint()).y(),rays_y=count.mapTo(settings,QPoint()).y();
   require(output_y<aa_y&&aa_y<rays_y&&settings->rect().contains(count.mapTo(settings,count.rect().center())),"Dialog common controls are not visible below Output size");
   if(rays==512&&!cancel){QDir().mkpath(output);require(settings->grab().save(output+"/bl-mesh-bake-controls.png"),"Cannot save dialog capture");}
   cases.append(QJsonObject{{"rays",rays},{"aa",aa},{"cancel",cancel},{"reopening",reopening},{"width",settings->width()},{"output_y",output_y},{"aa_y",aa_y},{"rays_y",rays_y}});
   if(cancel){auto *buttons=settings->findChild<QDialogButtonBox *>();require(buttons,"Missing real dialog button box");
    auto *button=buttons->button(QDialogButtonBox::Cancel);require(button,"Missing actual Cancel button");click(*button);
   }else click(control<QPushButton>(*settings,"saveBakeSettings"));
  }catch(...){error=std::current_exception();if(settings)settings->reject();}});
  const auto result=choose_mesh_bake(nullptr,document,scene);if(error)std::rethrow_exception(error);require(visited&&!result,"Controls-only dialog unexpectedly baked or did not run");
 };
 // Each selected value is committed by the real Save settings button, reopened,
 // and then changed/cancelled to prove cancellation preserves its saved value.
 int i=0;for(int rays:{16,17,128,256,511,512}){
  const int aa=std::array{1,2,4,8}[i++%4];visit(rays,aa,false,false);saved=mesh_bake_template(document.id());require(saved["rays"].toInt()==rays&&saved["sampling"].toInt()==aa,"Save settings omitted exact rays/AA");
  visit(rays==16?128:16,8,true,false);require(mesh_bake_template(document.id())==saved,"Cancel changed saved bake configuration");
 }
 visit(512,8,false,false);saved=mesh_bake_template(document.id());document.set_bake_configuration(saved);
 return {{"ui","MeshBakeDialog"},{"actual_modal_dialog",true},{"actual_Save_settings_and_Cancel",true},{"cases",cases},{"final_recipe",saved}};
}
inline void opened_dialog(TextureDocument &document,const SceneSnapshot &scene,const QString &output){
 // This is the same public cache synchronization performed by normal Open.
 apply_mesh_bake_template(document.id(),document.bake_configuration());
 std::exception_ptr error;bool visited=false;
 QTimer::singleShot(0,[&]{auto *settings=qobject_cast<QDialog *>(QApplication::activeModalWidget());try{require(settings,"Opened project dialog did not open");visited=true;auto &rays=control<QSpinBox>(*settings,"bakeSecondaryRays");auto &aa=control<QComboBox>(*settings,"bakeAntialiasing");require(rays.value()==512&&aa.currentData().toInt()==8,"Actual packed Open lost dialog512/AA8");auto *buttons=settings->findChild<QDialogButtonBox *>();require(buttons,"Opened dialog buttons absent");click(*buttons->button(QDialogButtonBox::Cancel));}catch(...){error=std::current_exception();if(settings)settings->reject();}});
 require(!choose_mesh_bake(nullptr,document,scene),"Open controls unexpectedly baked");if(error)std::rethrow_exception(error);require(visited,"Open dialog was not visited");
}
}
