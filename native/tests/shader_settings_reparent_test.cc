#include "ShaderController.hh"
#include "TextureDocument.hh"
#include "WorkspaceShell.hh"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>
#include <cstdio>
#include <stdexcept>
using namespace bts;
namespace {void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}}
int main(int argc,char **argv){
 QApplication app(argc,argv);
 try {
  SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Test";material.uniform_editable=true;scene.materials.push_back(material);
  ImportSummary imported;imported.materials.push_back({1,"Test",{1001}});NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;TextureDocument document(options,imported,scene);
  WorkspaceShell shell(new QWidget);shell.set_texture_sets({"Test"},0);shell.texture_sets()->item(0)->setData(Qt::UserRole,1);
  int applied=0,dirty=0;ShaderHooks hooks;hooks.document=[&]{return &document;};hooks.material=[]{return 1u;};hooks.apply=[&](const auto &){++applied;};hooks.dirty=[&]{++dirty;};hooks.repaint=[]{};hooks.end_input=[]{};hooks.log=[](const QString &message){throw std::runtime_error(message.toStdString());};
  ShaderController controller(&shell,std::move(hooks));auto *dialog=shell.findChild<QDialog *>("shaderSettings");require(dialog,"Shader dialog missing");auto *scroll=dialog->findChild<QScrollArea *>();auto *title=dialog->findChild<QLabel *>("shaderLegacySubsurfaceTitle");auto *workflow=dialog->findChild<QComboBox *>("shaderWorkflow");auto *openpbr=dialog->findChild<QWidget *>("openpbrParameters");auto *header=dialog->findChild<QLineEdit *>("shaderInstanceName")->parentWidget();require(scroll&&title&&workflow&&openpbr,"Real shader controls missing");auto *owner=title->parentWidget();
  require(!title->isHidden()&&openpbr->isHidden(),"Initial Principled controls incorrect");
  QTabWidget rendering;dialog->layout()->removeWidget(header);qobject_cast<QVBoxLayout *>(scroll->widget()->layout())->insertWidget(0,header);dialog->layout()->removeWidget(scroll);rendering.addTab(scroll,"Shader Settings");
  require(!dialog->findChild<QLabel *>("shaderLegacySubsurfaceTitle"),"Regression fixture did not reparent title outside dialog");
  // A same-named dialog child must never become a global fallback. This also
  // makes the historical implementation fail an assertion rather than crash.
  auto *decoy=new QLabel("Unrelated dialog label",dialog);decoy->setObjectName("shaderLegacySubsurfaceTitle");decoy->show();
  controller.sync();workflow->setCurrentIndex(1);require(document.set(1).shader_parameters.openpbr.has_value(),"OpenPBR edit did not reach model");require(title->isHidden()&&!openpbr->isHidden(),"Reparented OpenPBR sync did not hide owned legacy title");require(!decoy->isHidden(),"Sync changed unrelated dialog label");workflow->setCurrentIndex(0);require(!title->isHidden()&&openpbr->isHidden(),"Reparented Principled sync lost owned title");
  scroll->widget()->layout()->removeWidget(header);qobject_cast<QVBoxLayout *>(dialog->layout())->insertWidget(0,header);rendering.removeTab(rendering.indexOf(scroll));dialog->layout()->addWidget(scroll);delete decoy;
  require(title->parentWidget()==owner&&dialog->isAncestorOf(title),"Return changed shader title ownership");controller.sync();workflow->setCurrentIndex(1);require(title->isHidden()&&!openpbr->isHidden(),"Returned OpenPBR sync incorrect");workflow->setCurrentIndex(0);require(!title->isHidden()&&openpbr->isHidden(),"Returned Principled sync incorrect");require(applied==4&&dirty==4,"Workflow changes lost or duplicated model edits");
  std::puts("SHADER_SETTINGS_REPARENT_PASS real_controller=1 workflows=4 original_owner=1 decoy_unchanged=1");return 0;
 }catch(const std::exception &error){std::fprintf(stderr,"SHADER_SETTINGS_REPARENT_FAIL %s\n",error.what());return 1;}
}
