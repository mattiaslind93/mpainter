#pragma once
#include "NewProjectProfile.hh"
#include "BKE_global.hh"
#include "BKE_main.hh"
#include "IMB_colormanagement.hh"
#include "GPU_context.hh"
#include "GPU_pass.hh"
#include <QJsonArray>
#include <QJsonObject>
#include <QScopeGuard>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace bts {
// CPU regression only: no events/workers/GPU while the test-local global role
// is scoped. The live host Main remains unchanged; production calls this color
// initializer only once on its unused factory Main, before any worker/program.
inline QJsonObject verify_empty_workspace_color_cpu(){
 using namespace blender;
 const auto require=[](bool ok,const char *error){if(!ok)throw std::runtime_error(error);};
 require(!GPU_context_active_get(),"Empty workspace CPU test requires no GPU context");
 const auto compiled=GPU_pass_global_compilation_count();const std::string prior=IMB_colormanagement_working_space_get();
 const auto prior_matrix=IMB_colormanagement_get_scene_linear_to_xyz();
 const auto live=G_MAIN->colorspace;QJsonArray cases;
 {
  const auto restore=qScopeGuard([&]{IMB_colormanagement_working_space_set_from_name(prior.c_str());});
  for(const char *initial:{"Linear Rec.709","ACEScg"}){
   require(IMB_colormanagement_working_space_get_named_index(initial)>=0,"CPU test named input space missing");
   IMB_colormanagement_working_space_set_from_name(initial);
   std::unique_ptr<Main,decltype(&BKE_main_free)> detached(BKE_main_new(),BKE_main_free);
   std::strcpy(detached->colorspace.scene_linear_name,initial);
   detached->colorspace.scene_linear_to_xyz=IMB_colormanagement_get_scene_linear_to_xyz();
   NewProjectProfile::initialize_working_space(*detached);
   require(std::strcmp(detached->colorspace.scene_linear_name,"ACEScg")==0&&std::strcmp(IMB_colormanagement_working_space_get(),"ACEScg")==0,"Empty startup did not establish named canonical space");
   const auto first=detached->colorspace;
   NewProjectProfile::initialize_working_space(*detached); // Shared New profile color step remains idempotent.
   const auto active=IMB_colormanagement_get_scene_linear_to_xyz();
   for(int c=0;c<3;++c)for(int r=0;r<3;++r)require(first.scene_linear_to_xyz[c][r]==active[c][r]&&detached->colorspace.scene_linear_to_xyz[c][r]==active[c][r],"Recorded and active canonical matrices differ");
   require(std::strcmp(G_MAIN->colorspace.scene_linear_name,live.scene_linear_name)==0,"Detached initializer changed live Main name");
   for(int c=0;c<3;++c)for(int r=0;r<3;++r)require(G_MAIN->colorspace.scene_linear_to_xyz[c][r]==live.scene_linear_to_xyz[c][r],"Detached initializer changed live Main matrix");
   cases.append(QJsonObject{{"initial",initial},{"recorded_and_active","ACEScg"},{"exact_matrix_match",true},{"live_main_unchanged",true}});
  }
 }
 require(std::strcmp(IMB_colormanagement_working_space_get(),prior.c_str())==0,"CPU test did not restore actual global role");
 const auto restored=IMB_colormanagement_get_scene_linear_to_xyz();for(int c=0;c<3;++c)for(int r=0;r<3;++r)require(restored[c][r]==prior_matrix[c][r],"CPU test did not restore actual global matrix");
 require(!GPU_context_active_get()&&GPU_pass_global_compilation_count()==compiled,"Empty startup CPU check compiled or created GPU state");
 return {{"cases",cases},{"global_restored",true},{"cpu_only",true}};
}
}
