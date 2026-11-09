// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MaterialLayerSurfaceGraphObservation.hh"
#include <QJsonArray>
#include <QJsonObject>
#include <limits>
// Included after the existing dispatcher's always-active assert definition.
namespace bts::pd149_surface_test {
inline QJsonObject verify_surface_graph_predicate(){
 using blender::SOCK_FLOAT;using blender::SOCK_RGBA;using blender::SOCK_VECTOR;using blender::SOCK_SHADER;using blender::SOCK_INT;
 SurfaceGraphSnapshot s;s.status=SurfaceGraphStatus::Captured;s.host_draw=10;s.target_generation=2;s.selected_material_uid=42;
 s.distinct_evaluated=s.original_backlink_matches=s.distinct_evaluated_tree=s.tree_original_backlink_matches=true;
 SurfaceGraphState g;g.valid=true;g.output={2,"Material Output","ShaderNodeOutputMaterial",false};g.principled={1,"Principled BSDF","ShaderNodeBsdfPrincipled",false};
 SurfaceGraphSocket surface;surface.identifier="Surface";surface.type=SOCK_SHADER;surface.available=true;
 surface.incoming.push_back({g.principled,g.output,"BSDF","Surface",true,false,true});g.route.push_back(surface);
 const char *names[]{"Base Color","Metallic","Roughness","Normal"};const int components[]{4,1,1,3},types[]{SOCK_RGBA,SOCK_FLOAT,SOCK_FLOAT,SOCK_VECTOR};
 for(int i=0;i<4;++i){SurfaceGraphSocket v;v.identifier=names[i];v.type=types[i];v.components=components[i];v.available=true;g.inputs.push_back(v);}
 g.inputs[0].value={.25,.5,.75,1};g.inputs[1].value[0]=.25;g.inputs[2].value[0]=.5;
 s.original=s.evaluated=g;s.visible_slots.push_back({7,42,1,12,true,true,true});
 SurfaceGraphExpected e{42,2,g,g,s.visible_slots};SurfaceGraphPaint p{2,2,2,4,4,4,7,9,11,12,true};
 assert(surface_graph_matches(s,e,p));int positives=1,negatives=0;QJsonArray rejected;
 const auto reject=[&](const char *name,const auto &edit){auto a=s;auto b=e;auto c=p;edit(a,b,c);assert(!surface_graph_matches(a,b,c));rejected.append(name);++negatives;};
 for(auto status:{SurfaceGraphStatus::Disabled,SurfaceGraphStatus::NoSceneDraw,SurfaceGraphStatus::DrawFailed,SurfaceGraphStatus::NoSelection,SurfaceGraphStatus::NoEvaluated,SurfaceGraphStatus::MaterialOverride,SurfaceGraphStatus::UnsupportedGraph,SurfaceGraphStatus::NoVisibleSelectedSlot,SurfaceGraphStatus::Failed})reject("unavailable_status",[&](auto &a,auto &,auto &){a.status=status;});
 reject("unknown_status",[](auto &a,auto &,auto &){a.status=static_cast<SurfaceGraphStatus>(999);});
 reject("original_as_evaluated",[](auto &a,auto &,auto &){a.distinct_evaluated=false;});
 reject("foreign_original",[](auto &a,auto &,auto &){a.original_backlink_matches=false;});
 reject("original_tree_as_evaluated",[](auto &a,auto &,auto &){a.distinct_evaluated_tree=false;});
 reject("foreign_original_tree",[](auto &a,auto &,auto &){a.tree_original_backlink_matches=false;});
 reject("material_override",[](auto &a,auto &,auto &){a.material_override=true;});
 reject("wrong_selected_material",[](auto &a,auto &,auto &){a.selected_material_uid=43;});
 reject("wrong_target_generation",[](auto &a,auto &,auto &){a.target_generation=1;});
 reject("prior_draw",[](auto &a,auto &,auto &){a.host_draw=9;});
 reject("prior_phase_floor",[](auto &a,auto &,auto &c){c.phase_draw_floor=a.host_draw;});
 reject("phase_changed_during_Paint",[](auto &,auto &,auto &c){c.phase_after=3;});
 reject("prior_phase",[](auto &,auto &,auto &c){c.phase_before=1;});
 reject("revision_changed_during_Paint",[](auto &,auto &,auto &c){c.revision_after=5;});
 reject("prior_revision",[](auto &,auto &,auto &c){c.revision_before=3;});
 reject("no_later_Paint",[](auto &,auto &,auto &c){c.frames_after=c.frames_before;});
 reject("other_backend",[](auto &,auto &,auto &c){c.actual_eevee_route=false;});
 reject("missing_slot",[](auto &a,auto &,auto &){a.visible_slots.clear();});
 reject("wrong_slot",[](auto &a,auto &,auto &){a.visible_slots[0].slot=2;});
 reject("no_used_faces",[](auto &a,auto &b,auto &){a.visible_slots[0].faces=0;b.visible_slots=a.visible_slots;});
 reject("slot_original_object",[](auto &a,auto &b,auto &){a.visible_slots[0].evaluated_object=false;b.visible_slots=a.visible_slots;});
 reject("slot_original_material",[](auto &a,auto &b,auto &){a.visible_slots[0].evaluated_material=false;b.visible_slots=a.visible_slots;});
 reject("foreign_slot_material",[](auto &a,auto &b,auto &){a.visible_slots[0].material_original_uid=43;b.visible_slots=a.visible_slots;});
 reject("slot_not_selected",[](auto &a,auto &b,auto &){a.visible_slots[0].selected_binding=false;b.visible_slots=a.visible_slots;});
 reject("unsupported_socket_type",[](auto &a,auto &b,auto &){a.evaluated.inputs[2].type=SOCK_INT;b.evaluated=a.evaluated;});
 reject("wrong_canonical_input_type",[](auto &a,auto &b,auto &){a.evaluated.inputs[0].type=SOCK_VECTOR;a.evaluated.inputs[0].components=3;b.evaluated=a.evaluated;});
 reject("wrong_surface_route_type",[](auto &a,auto &b,auto &){a.evaluated.route[0].type=SOCK_RGBA;a.evaluated.route[0].components=4;b.evaluated=a.evaluated;});
 reject("stale_evaluated_default",[](auto &a,auto &,auto &){a.evaluated.inputs[0].value[0]=.75;});
 reject("wrong_original_default",[](auto &a,auto &,auto &){a.original.inputs[1].value[0]=.75;});
 reject("nonfinite_default",[](auto &a,auto &b,auto &){a.evaluated.inputs[2].value[0]=std::numeric_limits<double>::quiet_NaN();b.evaluated=a.evaluated;});
 reject("invalid_graph",[](auto &a,auto &b,auto &){a.evaluated.valid=false;b.evaluated=a.evaluated;});
 reject("muted_surface",[](auto &a,auto &b,auto &){a.evaluated.route[0].incoming[0].muted=true;b.evaluated=a.evaluated;});
 // Linked production route and detached constant reference are distinct states.
 auto linked=s;SurfaceGraphNode source{3,"__BTS_LayeredColor","ShaderNodeVectorMath",false};
 linked.original.inputs[0].incoming.push_back({source,g.principled,"Vector","Base Color",true,false,true});linked.evaluated=linked.original;
 auto le=e;le.original=le.evaluated=linked.original;assert(surface_graph_matches(linked,le,p));++positives;
 assert(!surface_graph_matches(linked,e,p));++negatives;rejected.append("expected_detached_reference_but_link_retained");
 assert(!surface_graph_matches(s,le,p));++negatives;rejected.append("expected_layered_link_but_reference_retained");
 auto reference=s;reference.original.inputs[0].value={.75,.125,.25,1};reference.evaluated=reference.original;
 auto re=e;re.original=re.evaluated=reference.original;assert(surface_graph_matches(reference,re,p));++positives;
 assert(!surface_graph_matches(reference,e,p));++negatives;rejected.append("wrong_reference_color");
 return {{"positive_cases",positives},{"negative_cases",negatives},{"rejected",rejected},
  {"pure_graph_and_Paint_value_predicate",true},{"shader_film_sampling_completion_tested",false}};
}
}
