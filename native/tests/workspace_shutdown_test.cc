// SPDX-License-Identifier: GPL-3.0-or-later
#include "NumericLocale.hh"
#include "WorkspaceShell.hh"
#include <QApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QScopeGuard>
#include <QScrollArea>
#include <QToolButton>
#include <cassert>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace bts { int run_workspace_shutdown_test(); }
using namespace bts;
namespace {
struct Calls {int edits=0,retired_edits=0;bool retired=false;NewProjectOptions options;BrushSettings brush;TextureSet set;};
std::unique_ptr<WorkspaceShell> workspace(Calls &calls){
  WorkspaceShell::Callbacks callbacks;
  callbacks.fill_projection_changed=[&](const FillProjection &){++calls.edits;if(calls.retired)++calls.retired_edits;};
  auto shell=std::make_unique<WorkspaceShell>(new QWidget,std::move(callbacks));
  shell->resize(1024,800);shell->show();shell->reset_reference_layout();
  auto &set=calls.set;
  set.uniform_editable=true;set.name="Shutdown fixture";
  TextureLayer fill;fill.kind=LayerKind::Fill;fill.name="Fill";
  set.layers={fill};set.selected_layer=fill.id;
  shell->show_texture_set(&set,&calls.options,&calls.brush);
  QApplication::processEvents();
  auto *undock=shell->findChild<QToolButton *>("propertiesUndock");assert(undock);undock->click();
  QApplication::processEvents();
  auto *floating=shell->findChild<QDialog *>("propertiesFloat");assert(floating&&floating->isVisible());
  calls.edits=calls.retired_edits=0;return shell;
}
void pending_text(WorkspaceShell &shell){
  auto *spin=shell.findChild<QDoubleSpinBox *>("fillOffset0");assert(spin&&!spin->keyboardTracking());
  auto *settings=shell.findChild<QToolButton *>("fillSettingsTab");assert(settings);settings->click();
  auto *transform=shell.findChild<QToolButton *>("fillTransformDisclosure");assert(transform);
  if(!transform->isChecked())transform->click();
  for(auto *parent=spin->parentWidget();parent;parent=parent->parentWidget())
    if(auto *scroll=qobject_cast<QScrollArea *>(parent)){scroll->ensureWidgetVisible(spin);break;}
  QApplication::processEvents();
  assert(spin->isVisible()&&spin->isEnabled()&&!spin->visibleRegion().isEmpty());
  auto *line=spin->findChild<QLineEdit *>();assert(line);
  spin->setFocus();
  const auto key=[&](int code,Qt::KeyboardModifiers modifiers,QString text){
    QKeyEvent press(QEvent::KeyPress,code,modifiers,text),release(QEvent::KeyRelease,code,modifiers,text);
    QApplication::sendEvent(spin,&press);QApplication::sendEvent(spin,&release);
  };
  key(Qt::Key_A,Qt::ControlModifier,"a");
  key(Qt::Key_0,Qt::NoModifier,"0");key(Qt::Key_Period,Qt::NoModifier,".");key(Qt::Key_5,Qt::NoModifier,"5");
  assert(line->text().toDouble()==.5&&spin->value()==0); // Real dirty text, not a setValue test.
}
void layer_toolbar(){
  TextureSet set;set.uniform_editable=true;
  TextureLayer base,fill;fill.kind=LayerKind::Fill;
  set.layers={base,fill};set.selected_layer=fill.id;
  NewProjectOptions options;BrushSettings brush;int deletes=0,captures=0;
  WorkspaceShell::Callbacks callbacks;
  callbacks.delete_selection=[&](const auto &effects,const auto &masks,const auto &layers){
    ++deletes;TextureDocument::remove_effects(set,effects);TextureDocument::remove_masks(set,masks);TextureDocument::delete_layers(set,layers);
  };
  callbacks.precompose_layer=[&](bool update){assert(!update);++captures;};
  WorkspaceShell shell(new QWidget,std::move(callbacks));
  auto *trash=shell.findChild<QToolButton *>("deleteLayer");
  auto *cache=shell.findChild<QToolButton *>("addPrecomposeLayer");
  assert(trash&&cache);
  shell.show_texture_set(nullptr,&options,&brush);assert(!trash->isEnabled()&&!cache->isEnabled());
  shell.show_texture_set(&set,&options,&brush);assert(trash->isEnabled()&&cache->isEnabled());
  cache->click();assert(captures==1);
  trash->click();assert(deletes==1&&set.layers.size()==1&&set.layers[0].id==base.id);
  set.layers[0].mask=LayerMask{};set.selected_mask=true;
  shell.setProperty("layerRowsOnly",true);shell.show_texture_set(&set,&options,&brush);
  assert(trash->isEnabled());trash->click();assert(deletes==2&&!set.layers[0].mask);
  set.selected_mask=false;LevelsEffect effect;set.layers[0].effects.push_back(effect);set.selected_effect=effect.id;
  shell.show_texture_set(&set,&options,&brush);assert(trash->isEnabled());trash->click();assert(deletes==3&&set.layers[0].effects.empty());
  set.layers.push_back(fill);set.selected_layer=fill.id;shell.show_texture_set(&set,&options,&brush);
  QKeyEvent key(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(shell.layer_stack(),&key);
  assert(deletes==4&&set.layers.size()==1);
  TextureLayer extra;set.layers.push_back(fill);set.layers.push_back(extra);set.selected_layer=extra.id;
  shell.show_texture_set(&set,&options,&brush);shell.layer_stack()->item(1)->setSelected(true);
  trash->click();assert(deletes==5&&set.layers.size()==1&&set.layers[0].id==base.id);
  set.layers.push_back(fill);set.layers.back().precompose_below=true;set.selected_layer=base.id;
  shell.show_texture_set(&set,&options,&brush);assert(!trash->isEnabled()&&!cache->isEnabled());
  set.selected_layer=fill.id;shell.show_texture_set(&set,&options,&brush);assert(trash->isEnabled()&&cache->isEnabled());
  set.uniform_editable=false;shell.show_texture_set(&set,&options,&brush);assert(!trash->isEnabled()&&!cache->isEnabled());
  shell.show_texture_set(nullptr,&options,&brush);assert(!trash->isEnabled()&&!cache->isEnabled());
  std::puts("Workspace toolbar: delete layers/mask/effect, multiselect, keyboard parity, Pre-compose dispatch, cached/read-only/closed state PASS");
}
}
int bts::run_workspace_shutdown_test(){
  layer_toolbar();
  // The unchanged Qt hide contract genuinely commits through FillPanel's own
  // connection context; disconnect(child,...,WorkspaceShell,...) cannot stop it.
  {Calls calls;auto shell=workspace(calls);pending_text(*shell);assert(calls.edits==0);
   shell->findChild<QDialog *>("propertiesFloat")->hide();assert(calls.edits>0);}
  // Retirement must precede the first hide/focus loss and remain idempotent.
  {Calls calls;auto shell=workspace(calls);pending_text(*shell);
   shell->retire_editor_callbacks();shell->retire_editor_callbacks();calls.retired=true;
   shell->findChild<QDialog *>("propertiesFloat")->hide();shell.reset();assert(calls.edits==0&&calls.retired_edits==0);}
  // Construction/error unwinding can reach the shell destructor without the
  // explicit application teardown. Its derived body must quiesce nested paths.
  {Calls calls;auto shell=workspace(calls);pending_text(*shell);calls.retired=true;
   shell.reset();assert(calls.edits==0&&calls.retired_edits==0);}
  {Calls calls;bool caught=false;
   try{auto shell=workspace(calls);const auto cleanup=qScopeGuard([&]()noexcept{shell->retire_editor_callbacks();shell.reset();});
       pending_text(*shell);calls.retired=true;throw std::runtime_error("retained fixture error");}
   catch(const std::runtime_error &){caught=true;}
   assert(caught&&calls.edits==0&&calls.retired_edits==0);}
  std::puts("Workspace nested pending editor hide control, retirement, destructor and exception cleanup PASS");
  return 0;
}
#ifdef BTS_WORKSPACE_SHUTDOWN_STANDALONE
int main(int argc,char **argv){
  QApplication app(argc,argv);initialize_numeric_locale();
  return bts::run_workspace_shutdown_test();
}
#endif
