#include "ShaderControllerTargetTest.hh"
#include "ShaderController.hh"
#include "WorkspaceShell.hh"
#include "MaterialLayeringStacks.hh"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <cassert>
#include <memory>
namespace bts {
namespace {
struct Fixture {
 NewProjectOptions options;BrushSettings brush;std::unique_ptr<TextureDocument> live;
 std::vector<std::unique_ptr<TextureDocument>> retired;std::vector<QJsonObject> retired_json;
 std::unique_ptr<WorkspaceShell> shell;std::unique_ptr<ShaderController> controller;
 std::function<void(TextureDocument &)> next;int ends=0,dirty=0;QString error;
 Fixture(){
  SceneSnapshot scene;MaterialSummary surface;surface.session_id=1;surface.name="Surface";surface.uniform_editable=true;scene.materials.push_back(surface);
  ImportSummary imported;imported.materials.push_back({1,"Surface"});options.resolution=DocumentResolution::Pixels128;
  live=std::make_unique<TextureDocument>(options,imported,scene);shell=std::make_unique<WorkspaceShell>(new QWidget,WorkspaceShell::Callbacks{});shell->show_texture_set(&live->set(1),&options,&brush);
  ShaderHooks hooks;hooks.document=[&]{return live.get();};hooks.material=[]{return 1u;};hooks.apply=[](const TextureSet&){};hooks.dirty=[&]{++dirty;};hooks.repaint=[]{};hooks.log=[&](const QString &text){error=text;};
  hooks.end_input=[&]{++ends;if(next){auto action=std::move(next);auto candidate=std::make_unique<TextureDocument>(*live);action(*candidate);retired_json.push_back(live->to_json());retired.push_back(std::move(live));live=std::move(candidate);}};
  controller=std::make_unique<ShaderController>(shell.get(),std::move(hooks));
 }
 ~Fixture(){controller.reset();shell->retire_editor_callbacks();shell.reset();}
 template<class T>T &get(const char *name){auto *widget=shell->findChild<T *>(name);assert(widget);return *widget;}
 void edit_normal(double value){get<QDoubleSpinBox>("shaderNormalIntensity").setValue(value);controller->finish();}
 void untouched()const{assert(retired.size()==retired_json.size());for(std::size_t i=0;i<retired.size();++i)assert(retired[i]->to_json()==retired_json[i]);}
};
}
QJsonObject test_shader_controller_layering_targets(){
 // Real controller signal path: end_input replaces the document just as paint/property commit does.
 {Fixture f;f.next=[](auto&){};f.edit_normal(1.5);assert(f.ends==1&&f.dirty==1&&f.live->set(1).shader_parameters.normal_intensity==1.5f);f.untouched();
  f.next=[](auto&){};f.get<QPushButton>("shaderUndo").click();assert(f.ends==2&&f.live->set(1).shader_parameters.normal_intensity==1&&f.get<QPushButton>("shaderRedo").isEnabled());f.untouched();
  f.next=[](auto&){};f.get<QPushButton>("shaderRedo").click();assert(f.ends==3&&f.live->set(1).shader_parameters.normal_intensity==1.5f);f.untouched();}
 // A changed shader input cannot be overwritten by a candidate prepared before end_input.
 {Fixture f;f.next=[](auto &doc){auto state=doc.shader_instances();auto p=state.for_set(doc.set(1).id).parameters;p.ao_intensity=.4f;state.parameters(state.assignments.at(doc.set(1).id),p);doc.set_shader_instances(std::move(state));};f.edit_normal(2);assert(f.ends==1&&f.dirty==0&&f.live->set(1).shader_parameters.normal_intensity==1&&f.live->set(1).shader_parameters.ao_intensity==.4f&&!f.get<QPushButton>("shaderUndo").isEnabled()&&f.error.contains("changed"));f.untouched();}
 // Mode changes without shader-library changes still require a fresh workflow check.
 {Fixture f;f.next=[](auto &doc){configure_material_layering(doc.set(1),true);};f.get<QComboBox>("shaderWorkflow").setCurrentIndex(1);assert(f.ends==1&&f.dirty==0&&f.live->set(1).material_layering.enabled&&!f.live->set(1).shader_parameters.openpbr&&!f.get<QPushButton>("shaderUndo").isEnabled()&&f.error.contains("Disable"));f.untouched();}
 // Rejected history retains both stacks and never mutates an old document snapshot.
 {Fixture f;f.edit_normal(1.5);const int dirty=f.dirty;f.next=[](auto &doc){auto state=doc.shader_instances();auto p=state.for_set(doc.set(1).id).parameters;p.ao_intensity=.4f;state.parameters(state.assignments.at(doc.set(1).id),p);doc.set_shader_instances(std::move(state));};f.get<QPushButton>("shaderUndo").click();assert(f.dirty==dirty&&f.live->set(1).shader_parameters.normal_intensity==1.5f&&f.live->set(1).shader_parameters.ao_intensity==.4f&&f.get<QPushButton>("shaderUndo").isEnabled()&&!f.get<QPushButton>("shaderRedo").isEnabled()&&f.error.contains("changed"));f.untouched();
  f.get<QPushButton>("shaderUndo").click();assert(f.live->set(1).shader_parameters.normal_intensity==1&&f.get<QPushButton>("shaderRedo").isEnabled());}
 {Fixture f;f.get<QComboBox>("shaderWorkflow").setCurrentIndex(1);f.get<QComboBox>("shaderWorkflow").setCurrentIndex(0);const int dirty=f.dirty;f.next=[](auto &doc){configure_material_layering(doc.set(1),true);};f.get<QPushButton>("shaderUndo").click();assert(f.dirty==dirty&&f.live->set(1).material_layering.enabled&&!f.live->set(1).shader_parameters.openpbr&&f.get<QPushButton>("shaderUndo").isEnabled()&&!f.get<QPushButton>("shaderRedo").isEnabled()&&f.error.contains("Disable"));f.untouched();}
 return {{"same_project_document_swap_change_undo_redo",true},{"changed_source_rejected",true},{"workflow_rechecked_after_input",true},{"rejected_history_retained",true},{"retired_document_snapshots_untouched",true}};
}
}
