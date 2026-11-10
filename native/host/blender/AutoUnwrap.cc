// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceAvailability.hh"
#include "AutoUnwrap.hh"
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include "BKE_context.hh"
#include "BKE_attribute.hh"
#include "BLI_listbase.h"
#include "BKE_editmesh.hh"
#include "BKE_layer.hh"
#include "BKE_main.hh"
#include "BKE_mesh.hh"
#include "BKE_report.hh"
#include "DNA_layer_types.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_windowmanager_types.h"
#include "ED_object.hh"
#include "GEO_uv_pack.hh"
#include "RNA_access.hh"
#include "WM_api.hh"
#include "WM_types.hh"
#include "bmesh.hh"
namespace bts {
bool edit_auto_unwrap_options(QWidget *parent,AutoUnwrapOptions &options){
  QDialog dialog(parent);dialog.setObjectName("autoUnwrapOptions");dialog.setWindowTitle("Automatic UV Unwrapping");dialog.resize(500,328);
  auto *layout=new QVBoxLayout(&dialog);auto *form=new QFormLayout;layout->addLayout(form);
  QComboBox *policies[3];const char *labels[]={"Seams","UV islands","Packing"};const char *names[]={"unwrapSeams","unwrapIslands","unwrapPacking"};const UvDataPolicy values[]={options.seams,options.islands,options.packing};
  for(int i=0;i<3;++i){auto *combo=policies[i]=new QComboBox(&dialog);combo->setObjectName(names[i]);combo->addItems({"Generate only missing data","Recompute all"});combo->setCurrentIndex(int(values[i]));form->addRow(labels[i],combo);}
  policies[0]->setToolTip("Retain authored seam edges, or recalculate cuts when generating UV islands.");
  const auto dependencies=[&]{const bool seams=policies[0]->currentIndex()==1;if(seams)policies[1]->setCurrentIndex(1);policies[1]->setEnabled(!seams);const bool islands=policies[1]->currentIndex()==1;if(islands)policies[2]->setCurrentIndex(1);policies[2]->setEnabled(!islands);};
  QObject::connect(policies[0],&QComboBox::currentIndexChanged,&dialog,[&]{dependencies();});QObject::connect(policies[1],&QComboBox::currentIndexChanged,&dialog,[&]{dependencies();});dependencies();
  auto *hard_surface=new QCheckBox("Hard surface",&dialog);hard_surface->setObjectName("unwrapHardSurface");hard_surface->setChecked(options.hard_surface);hard_surface->setToolTip("Orthographic projection groups and axis-aligned island packing. Existing UVs are retained with Generate only missing data; Recompute all rebuilds them.");form->addRow("Projection",hard_surface);
  auto *margin=new QComboBox(&dialog);margin->setObjectName("unwrapMargin");margin->addItems({"No margin (0%)","Small (default, 0.2%)","Medium (0.5%)","Large (1%)"});margin->setCurrentIndex(options.margin);form->addRow("Margin size",margin);layout->addStretch();
  auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  if(dialog.exec()!=QDialog::Accepted)return false;options.seams=UvDataPolicy(policies[0]->currentIndex());options.islands=UvDataPolicy(policies[1]->currentIndex());options.packing=UvDataPolicy(policies[2]->currentIndex());options.margin=margin->currentIndex();options.hard_surface=hard_surface->isChecked();return true;
}
namespace {
using namespace blender;
void execute(bContext *context,const char *name,const std::function<void(PointerRNA &)> &configure={},bool allow_unchanged=false){
  auto *type=WM_operatortype_find(name,true);if(!type||!type->exec)throw std::runtime_error(std::string("Native UV operator unavailable: ")+name);
  auto properties=WM_operator_properties_create_ptr(type);WM_operator_properties_default(&properties,false);ReportList reports{};BKE_reports_init(&reports,RPT_STORE);wmOperator op{};op.type=type;op.ptr=&properties;op.properties=static_cast<IDProperty *>(properties.data);op.reports=&reports;
  try{if(configure)configure(properties);const auto status=type->exec(context,&op);if(status!=OPERATOR_FINISHED&&!(allow_unchanged&&status==OPERATOR_CANCELLED))throw std::runtime_error(std::string("Native UV operation failed: ")+name);for(const Report &report:reports.list)if(report.type>=RPT_ERROR)throw std::runtime_error(std::string("Native UV operation: ")+report.message);}
  catch(...){BKE_reports_free(&reports);WM_operator_properties_free(&properties);throw;}BKE_reports_free(&reports);WM_operator_properties_free(&properties);
}
}
void generate_missing_uvs(blender::Main &main,blender::Scene &scene,blender::bContext *context,const AutoUnwrapOptions &options){
  using namespace blender;if(!options.enabled)return;if(options.margin<0||options.margin>3)throw std::invalid_argument("Invalid automatic UV margin");
  scene.toolsettings->uv_flag|=UV_FLAG_SELECT_SYNC;
  constexpr float margins[]={0,.002f,.005f,.01f};auto *layer=BKE_view_layer_default_view(&scene);BKE_view_layer_synced_ensure(main,&scene,layer);
  const auto packing=[&](PointerRNA &p){RNA_float_set(&p,"margin",margins[options.margin]);RNA_enum_set(&p,"margin_method",ED_UVPACK_MARGIN_FRACTION);};
  for(Object &object:main.objects){
    if(object.type!=OB_MESH)continue;auto &mesh=*reinterpret_cast<Mesh *>(object.data);if(!mesh.faces_num)continue;
    const bool had_uv=!mesh.uv_map_names().is_empty();const bool islands=!had_uv||options.islands==UvDataPolicy::Recompute||options.seams==UvDataPolicy::Recompute;
    if(had_uv&&!islands&&options.seams==UvDataPolicy::Missing&&options.packing==UvDataPolicy::Missing)continue;

    auto *base=BKE_view_layer_base_find(layer,&object);if(!base)throw std::runtime_error("Automatic UV mesh is not in the staging view layer");layer->basact=base;
    if(!ed::object::editmode_enter_ex(&main,&scene,&object,ed::object::EM_NO_CONTEXT))throw std::runtime_error("Cannot enter staged UV edit mode");
    try{
      auto *edit=BKE_editmesh_from_object(&object);BM_mesh_elem_hflag_enable_all(edit->bm,BM_VERT|BM_EDGE|BM_FACE,BM_ELEM_SELECT,false);
      bool seams=false;BMIter iter;BMEdge *edge;BM_ITER_MESH(edge,&iter,edit->bm,BM_EDGES_OF_MESH)seams|=BM_elem_flag_test(edge,BM_ELEM_SEAM);
      if(islands){
        if(options.hard_surface||options.seams==UvDataPolicy::Recompute||!seams){
          if(options.seams==UvDataPolicy::Recompute||!seams)BM_ITER_MESH(edge,&iter,edit->bm,BM_EDGES_OF_MESH)BM_elem_flag_disable(edge,BM_ELEM_SEAM);
          execute(context,"UV_OT_smart_project",[&](PointerRNA &p){RNA_float_set(&p,"island_margin",margins[options.margin]);RNA_enum_set(&p,"margin_method",ED_UVPACK_MARGIN_FRACTION);RNA_boolean_set(&p,"correct_aspect",false);if(options.hard_surface){RNA_enum_set(&p,"rotate_method",ED_UVPACK_ROTATION_AXIS_ALIGNED);RNA_float_set(&p,"angle_limit",float(std::acos(-1.)/4));}});
          execute(context,"UV_OT_seams_from_islands",{},true);
        }
        if(!options.hard_surface)execute(context,"UV_OT_unwrap",[&](PointerRNA &p){packing(p);RNA_enum_set(&p,"method",0);RNA_boolean_set(&p,"correct_aspect",false);RNA_boolean_set(&p,"fill_holes",true);});
      }else if(options.seams==UvDataPolicy::Recompute){
        BM_ITER_MESH(edge,&iter,edit->bm,BM_EDGES_OF_MESH)BM_elem_flag_disable(edge,BM_ELEM_SEAM);execute(context,"UV_OT_seams_from_islands",{},true);
      }
      if(islands||options.packing==UvDataPolicy::Recompute)execute(context,"UV_OT_pack_islands",[&](PointerRNA &p){packing(p);RNA_boolean_set(&p,"rotate",true);if(options.hard_surface)RNA_enum_set(&p,"rotate_method",ED_UVPACK_ROTATION_AXIS_ALIGNED);});
      ed::object::editmode_exit_ex(&main,&scene,&object,ed::object::EM_FREEDATA);
    }catch(...){ed::object::editmode_free_ex(&main,&object);object.mode=OB_MODE_OBJECT;throw;}
    if(islands){const auto uv=mesh.attributes().lookup<float2>(mesh.active_or_default_uv_map_name(),bke::AttrDomain::Corner);if(!uv)throw std::runtime_error("Native unwrap produced no UV map");double area=0;for(const auto &triangle:mesh.corner_tris()){const auto a=uv.varray[triangle.x],b=uv.varray[triangle.y],c=uv.varray[triangle.z];area+=std::abs(double(b.x-a.x)*(c.y-a.y)-double(b.y-a.y)*(c.x-a.x))*.5;}if(!std::isfinite(area)||area<1e-10)throw std::runtime_error("Native unwrap produced collapsed islands; supply usable seam edges or recompute seams");}
  }
}
}
