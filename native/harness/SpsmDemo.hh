// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ViewportDemoCapture.hh"
#include "Performance.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <functional>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLayout>
#include <QEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>

namespace bts {
// Explicit test mode only. Exercises the artist New/Import/Shelf/history/save
// routes; it never opens a window or writes settings in an artist session.
class SpsmDemo final : public QObject {
  QWidget *w,*v;QString directory,mesh;std::function<QJsonObject()> state;
  QTimer timer,dialogs;QStringList files;int phase=0,index=0,ticks=0,before_layers=0,before_undo=0;
  bool busy=false,inputs_hidden=false,source_cache_removed=false;QJsonArray captures,reports,exports;
  template<class T>T *get(const char *name){auto *p=w->findChild<T *>(name);if(!p)qFatal("SPSM demo missing control: %s",name);return p;}
  void action(const char *name){get<QAction>(name)->trigger();}
  int layers(const QJsonObject &s)const{return s["document"].toObject()["texture_sets"].toArray().at(0).toObject()["layers"].toArray().size();}
  void capture(const QString &name){auto s=state();s["stage"]=name;captures.append(s);capture_demo_window(w,v).save(directory+"/"+name+".png");}
  void export_gpu_maps(){
    timer.stop();
    QTimer::singleShot(100,this,[this]{
      auto *dialog=get<QDialog>("flattenExportDialog");const auto path=directory+QString("/gpu-export-%1").arg(index);QDir().mkpath(path);
      dialog->findChild<QLineEdit *>("flattenExportDirectory")->setText(path);
      dialog->findChild<QPushButton *>("runFlattenExport")->click();
      if(!dialog->property("flattenExportError").toString().isEmpty()||!dialog->property("flattenExportFinished").toBool())qFatal("SPSM GPU export failed: %s",qPrintable(dialog->property("flattenExportError").toString()));
      const auto files=dialog->property("flattenExportFiles").toStringList();const auto probes=dialog->property("flattenExportProbes").toJsonArray();
      if(files.empty()||probes.empty())qFatal("SPSM GPU export produced no verified pixels");for(const auto &file:files)if(file.contains("_normal."))qFatal("Internal Normal was publicly exported");
      exports.append(QJsonObject{{"material",index},{"files",QJsonArray::fromStringList(files)},{"gpu_source_checks",probes}});dialog->accept();
    });
    action("exportFlattenedGroups");timer.start();
  }
  void service_dialogs(){
    for(auto *widget:QApplication::topLevelWidgets()){
      if(!widget->isVisible())continue;
      if(auto *box=qobject_cast<QMessageBox *>(widget))qFatal("SPSM demo message: %s",qPrintable(box->text()));
      if(widget->objectName()=="meshBakeDialog"&&!widget->property("spsmPrepared").toBool()){
        widget->setProperty("spsmPrepared",true);auto *maps=get<QListWidget>("bakeMapTypes");for(int role=0;role<maps->count();++role)maps->item(role)->setCheckState(role==1||role>=3?Qt::Checked:Qt::Unchecked);
        get<QCheckBox>("bakeUseProjectMesh")->setChecked(true);auto *resolution=get<QComboBox>("bakeResolution");resolution->setCurrentIndex(resolution->findData(512));get<QSpinBox>("bakeSecondaryRays")->setValue(32);get<QPushButton>("startMeshBake")->click();
      }
      if(widget->objectName()=="meshBakeProgress"){
        auto *finish=widget->findChild<QPushButton *>("finishMeshBake");if(finish&&finish->text()=="OK")finish->click();else if(finish&&finish->text()=="Close")qFatal("SPSM comparison bake failed: %s",qPrintable(widget->findChild<QLabel *>("meshBakeProgressStatus")->text()));
      }
      if(widget->objectName()=="resourceImportDialog"&&!widget->property("spsmConfigured").toBool()){
        widget->setProperty("spsmConfigured",true);
        widget->findChild<QLineEdit *>("resourceImportPath")->setText(files.at(index));
        if(widget->findChild<QComboBox *>("resourceImportUsage")->currentIndex()!=3)qFatal("SPSM usage did not default to Material preset");
        auto *options=widget->findChild<QGroupBox *>("spsmBitmapOptions");
        if(!options||!options->isChecked())qFatal("SPSM native bitmap interpretation must default enabled");
        const auto color_policy=qEnvironmentVariable("BTS_SPSM_BITMAP_COLOR_POLICY");
        if(qEnvironmentVariableIsSet("BTS_SPSM_BITMAP_POLICY")||!color_policy.isEmpty()) {
          options->setChecked(true);
          auto *roughness=options->findChild<QCheckBox *>("spsmBitmapRoughness");
          auto *color=options->findChild<QComboBox *>("spsmBitmapColor");
          if(!roughness||!roughness->isChecked()||!color||color->currentData()!="native-linear-rec709-rgb-levels-ignore-a")qFatal("SPSM per-channel defaults changed");
          roughness->setChecked(qEnvironmentVariableIsSet("BTS_SPSM_BITMAP_POLICY"));
          if(!color_policy.isEmpty()){
            const int choice=color->findData(color_policy);
            if(choice<=0)qFatal("SPSM demo unknown named Color interpretation");
            color->setCurrentIndex(choice);
          }
          for(const char *name:{"spsmBitmapBytes","spsmBitmapRows","spsmBitmapAlpha","spsmBitmapUv","spsmBitmapSampling"}) {
            auto *choice=options->findChild<QComboBox *>(name);
            if(!choice||choice->currentIndex()!=1)qFatal("SPSM bitmap interpretation must default to native choices");
            choice->setCurrentIndex(1);
          }
          if(qEnvironmentVariableIsSet("BTS_SPSM_LEVELS_POLICY")) {
            auto *levels=options->findChild<QCheckBox *>("spsmNativeScalarLevels");
            if(!levels||!levels->isChecked())qFatal("SPSM native scalar Levels must default enabled");
            levels->setChecked(true);
          }
          if(qEnvironmentVariableIsSet("BTS_SPSM_MASK_BITMAP_POLICY")) {
            auto *mask=options->findChild<QCheckBox *>("spsmNativeBitmapMask");
            if(!mask||!mask->isChecked())qFatal("SPSM native bitmap mask must default enabled");
            mask->setChecked(true);
          }
        }
        widget->findChild<QComboBox *>("resourceImportScope")->setCurrentIndex(1);
        widget->resize(560,545);
        // Let the real nested form/scroll layouts settle after changing the
        // source type and policies. A synchronous grab here captured stale row
        // geometry before Qt processed its pending LayoutRequest events.
        QTimer::singleShot(0,widget,[this,widget]{
          QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);
          widget->layout()->activate();
          auto *scroll=widget->findChild<QScrollArea *>("resourceImportScroll");
          auto *buttons=widget->findChild<QDialogButtonBox *>();
          if(!scroll||!buttons||widget->width()!=560||widget->height()!=545||
             buttons->geometry().bottom()>=widget->height()||
             scroll->geometry().bottom()>=buttons->geometry().top())qFatal("SPSM import controls must fit a narrow scrollable dialog");
          auto *hint=widget->findChild<QLabel *>("spsmBitmapInterpretationHint");
          auto *roughness=widget->findChild<QCheckBox *>("spsmBitmapRoughness");
          if(!hint||!roughness||hint->height()<hint->fontMetrics().lineSpacing()*int(hint->text().count('\n')+1)||
             hint->geometry().bottom()>=roughness->geometry().top())
            qFatal("SPSM interpretation hint lines and next control must not overlap");
          widget->grab().save(directory+QString("/import-options-%1.png").arg(index));
          buttons->button(QDialogButtonBox::Ok)->click();
        });
      }
      if(widget->objectName()=="spsmImportReport")qFatal("Normal Shelf application unexpectedly opened a modal SPSM report");

    }
  }
  void advance(){
    if(busy)return;if(++ticks>4800)qFatal("SPSM demo timed out phase=%d file=%d",phase,index);
    const auto s=state();v->update();if(s["composite_pending"].toBool()||s["property_pending"].toBool()||s["save_pending"].toBool()||s["open_pending"].toBool())return;
    if(!s["open_error"].toString().isEmpty())qFatal("SPSM Open failed: %s",qPrintable(s["open_error"].toString()));
    if(!s["save_error"].toString().isEmpty())qFatal("SPSM save failed: %s",qPrintable(s["save_error"].toString()));
    switch(phase){
      case 0:phase=1;action("newProject");break;
      case 1:{auto *d=get<QDialog>("newProjectDialog");d->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *r=d->findChild<QComboBox *>("documentResolution");r->setCurrentIndex(r->findData(512));d->findChild<QCheckBox *>("preserveUvTiles")->setChecked(true);phase=2;busy=true;d->findChild<QPushButton *>("createProject")->click();busy=false;break;}
      case 2:{if(!s["has_document"].toBool())qFatal("SPSM GUI New failed");if(index==0&&!s["spsm_baked_maps"].isArray()){phase=10;busy=true;QTimer::singleShot(0,this,[this]{action("bakeMeshMapsAction");busy=false;});break;}before_layers=layers(s);before_undo=s["undo_count"].toInt();phase=3;busy=true;get<QToolButton>("importResources")->click();busy=false;break;}
      case 3:{auto *list=get<QListWidget>("shelfAssets");QListWidgetItem *found=nullptr;const auto name=QFileInfo(files.at(index)).completeBaseName();for(int row=0;row<list->count();++row)if(list->item(row)->text()==name)found=list->item(row);if(!found)qFatal("Imported SPSM missing from Shelf: %s",qPrintable(name));if(!(found->flags()&Qt::ItemIsEnabled))qFatal("SPSM Shelf entry incorrectly requires Worn Metal mesh maps");phase=4;performance::begin("spsm_"+name.toStdString());list->setCurrentItem(found);QMetaObject::invokeMethod(list,"itemDoubleClicked",Qt::DirectConnection,Q_ARG(QListWidgetItem*,found));break;}
      case 4:if(layers(s)==before_layers)return;if(s["spsm_baked_maps"].toArray().size()!=5)qFatal("Applied material lost baked maps");if(s["undo_count"].toInt()!=before_undo+1)qFatal("SPSM apply must create exactly one Undo step");action("checkSpsmGpuScope");capture(QString("applied-%1").arg(index));if(qEnvironmentVariableIsSet("BTS_FLATTEN_VERIFY_OUTPUT"))export_gpu_maps();phase=5;action("undoDocument");break;
      case 5:if(layers(s)!=before_layers)qFatal("SPSM Undo did not restore the old stack");capture(QString("undo-%1").arg(index));phase=6;action("redoDocument");break;
      case 6:if(layers(s)<=before_layers)qFatal("SPSM Redo did not restore imported layers");capture(QString("redo-%1").arg(index));if(++index<files.size()){phase=9;action("undoDocument");break;}phase=7;action("guiSkewSave");break;
      case 7:if(!QFile::exists(directory+"/skew.blend"))return;capture("saved");
        if(qEnvironmentVariableIsSet("BTS_SPSM_HIDE_SOURCES")){
          const auto input=QFileInfo(qEnvironmentVariable("BTS_SPSM_FIXTURES")).canonicalFilePath();
          if(input!=QFileInfo(directory+"/inputs").canonicalFilePath()||input.isEmpty())qFatal("SPSM absence test requires own private input copies");
          if(!QDir(directory).rename("inputs","inputs.held"))qFatal("Cannot hide private SPSM input copies");inputs_hidden=true;
          const auto cache=QFileInfo(qEnvironmentVariable("BTS_RESOURCE_CACHE")).canonicalFilePath();
          if(cache!=QFileInfo(directory+"/cache").canonicalFilePath()||cache.isEmpty())qFatal("SPSM absence test requires own private cache");
          if(!QDir(cache+"/spsm").removeRecursively())qFatal("Cannot remove own disposable SPSM source cache");source_cache_removed=true;
        }
        phase=8;busy=true;action("guiSkewOpen");busy=false;break;
      case 9:if(layers(s)!=before_layers)qFatal("SPSM per-material baseline was not restored");phase=2;break;
      case 10:{const auto maps=s["spsm_baked_maps"].toArray();if(maps.size()!=5)qFatal("SPSM comparison requires five real baked maps");for(const auto &map:maps)if(map.toObject()["covered"].toInt()<=0)qFatal("SPSM comparison baked map has no covered mesh pixels");capture("baked-five-maps");phase=2;break;}
      case 8:action("checkSpsmGpuScope");capture("reopened");timer.stop();dialogs.stop();{QFile output(directory+"/spsm-import.json");if(!output.open(QIODevice::WriteOnly))qFatal("SPSM proof could not be written");output.write(QJsonDocument(QJsonObject{{"captures",captures},{"reports",reports},{"gpu_flattened_exports",exports},{"source_files",QJsonArray::fromStringList(files)},{"appearance_verified",false},{"proof_scope",QJsonObject{{"lifecycle","File New, five baked maps, Shelf apply, Undo/Redo, packed reopen"},{"whole_material_cpu_parity","Per-capture passed or unavailable; no identity reference for spatial filters"},{"spatial_kernel_parity","Separate independent production-kernel proof required"},{"authored_appearance","unverified"}}},{"bake_resolution",512},{"bake_rays",32},{"mesh",mesh},{"original_inputs_hidden_during_open",inputs_hidden},{"original_source_cache_removed_before_open",source_cache_removed}}).toJson());}QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;
    }
  }
 public:SpsmDemo(QWidget *window,QWidget *viewport,QString output,QString fixture,std::function<QJsonObject()> snapshot):QObject(window),w(window),v(viewport),directory(output),mesh(fixture),state(std::move(snapshot)){
    const QDir sources(qEnvironmentVariable("BTS_SPSM_FIXTURES"));for(const auto &name:sources.entryList({"*.spsm"},QDir::Files,QDir::Name))files.append(sources.filePath(name));if(files.isEmpty())qFatal("SPSM fixture directory contains no .spsm files");QDir().mkpath(directory);
    connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start(100);connect(&dialogs,&QTimer::timeout,this,[this]{service_dialogs();});dialogs.start(25);
  }
};
}
