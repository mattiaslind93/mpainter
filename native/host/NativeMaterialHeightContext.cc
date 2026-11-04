#include <QtCore>
#include "NativeMaterialHeightContext.hh"
#include "NativeMaterialContext.hh"
#include "BKE_anim_data.hh"
#include "BKE_global.hh"
#include "BKE_main.hh"
#include "BLI_listbase.h"
#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_build.hh"
#include "DNA_anim_types.h"
#include "DNA_action_types.h"
#include "DNA_curve_types.h"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include <QCoreApplication>
#include <QThread>

namespace bts {
void require_native_height_no_drivers(blender::Main &main){
 blender::BKE_animdata_main_cb(&main,[](blender::ID *,blender::AnimData *animation){if(animation->drivers.first)throw std::invalid_argument("Raw authored Height currently excludes native drivers, including embedded node-tree drivers");});
}
std::map<std::uint32_t,NativeMaterialGpuContext> prepare_native_height_context(
 blender::Main &main,blender::Scene &scene,blender::ViewLayer &view_layer,const std::set<std::uint32_t> &materials,const std::atomic_bool &cancel){
 using namespace blender;
 if(auto *app=QCoreApplication::instance();app&&QThread::currentThread()==app->thread())throw std::logic_error("Pending native Height context requires a worker");
 const auto check=[&]{if(cancel.load())throw std::runtime_error("Native Height context preparation cancelled");};check();require_native_height_no_drivers(main);
 for(const Scene &candidate:main.scenes)if(candidate.rigidbody_world)throw std::invalid_argument("Raw authored Height Open currently excludes rigid-body world evaluation");
 std::size_t modifier_count=0;std::set<Mesh *> meshes;
 for(auto &object:main.objects){
  if(object.constraints.first||object.rigidbody_object||object.rigidbody_constraint||object.soft||object.particlesystem.first)throw std::invalid_argument("Raw authored Height Open currently excludes object constraints and physics evaluation");
  if(object.pose)for(const auto &channel:object.pose->chanbase)if(channel.constraints.first)throw std::invalid_argument("Raw authored Height Open currently excludes pose-bone constraints");
  if(object.parent&&(object.partype&PARTYPE)!=PAROBJECT)throw std::invalid_argument("Raw authored Height Open currently requires Object parenting; bone/vertex parenting is not enabled");
  if(object.parent&&object.parent->type==OB_CURVES_LEGACY&&object.parent->data&&(reinterpret_cast<Curve *>(object.parent->data)->flag&CU_PATH))throw std::invalid_argument("Raw authored Height Open currently excludes curve-path Object parenting because it consumes evaluated geometry");
  for(const auto &modifier:object.modifiers){(void)modifier;modifier_count=resource_sum(modifier_count,1);}
  if(object.type==OB_MESH&&object.data)meshes.insert(reinterpret_cast<Mesh *>(object.data));
 }
 struct Suppressed {
  struct MeshState{Mesh *mesh;decltype(Mesh::texspace_flag) flag;std::array<float,3> location,size;};
  std::vector<std::pair<ModifierData *,ModifierMode>> modes;std::vector<MeshState> texspaces;
  ~Suppressed(){for(const auto &saved:texspaces){saved.mesh->texspace_flag=saved.flag;std::copy_n(saved.location.data(),3,saved.mesh->texspace_location);std::copy_n(saved.size.data(),3,saved.mesh->texspace_size);}for(const auto &[modifier,mode]:modes)modifier->mode=mode;}
 } suppressed;

 suppressed.modes.reserve(modifier_count);suppressed.texspaces.reserve(meshes.size());
 for(auto *mesh:meshes){Suppressed::MeshState saved; saved.mesh=mesh;saved.flag=mesh->texspace_flag;std::copy_n(mesh->texspace_location,3,saved.location.data());std::copy_n(mesh->texspace_size,3,saved.size.data());suppressed.texspaces.push_back(saved);}
 for(auto &object:main.objects)for(auto &modifier:object.modifiers)suppressed.modes.emplace_back(&modifier,modifier.mode);
 // All allocations precede mode mutation. Geometry modifiers do not enter the
 // admitted Object-only parent transform/color or ORIGINAL-mesh texspace
 // contract. This includes our preview subdivision without trusting its name.
 // Blender may still evaluate virtual ShapeKey geometry: no output mesh is
 // consumed, and this is not a promise of zero geometry evaluation.
 for(const auto &[modifier,mode]:suppressed.modes)modifier->mode=mode&~(eModifierMode_Realtime|eModifierMode_Render|eModifierMode_Editmode);
 const auto *global_main=G_MAIN;
 // This variable is declared AFTER the restoration scope: graph destruction
 // necessarily precedes exact mode/texspace restoration on every unwind path.
 std::unique_ptr<Depsgraph,decltype(&DEG_graph_free)> graph(DEG_graph_new(&main,&scene,&view_layer,DAG_EVAL_VIEWPORT),DEG_graph_free);
 if(!graph)throw std::runtime_error("Cannot prepare private native Height context");
 if(DEG_is_active(graph.get()))throw std::logic_error("Pending Height context must not become the active depsgraph");
 DEG_graph_relations_update(graph.get());check();DEG_evaluate_on_refresh(graph.get(),DEG_EVALUATE_SYNC_WRITEBACK_NO);check();
 std::map<std::uint32_t,NativeMaterialGpuContext> contexts;
 for(auto material:materials){check();NativeMaterialGpuContext context;bind_native_material_objects(main,graph.get(),material,context);contexts.emplace(material,std::move(context));}
 graph.reset();if(G_MAIN!=global_main)throw std::logic_error("Pending Height context changed the global Main");
 return contexts;
}
}
