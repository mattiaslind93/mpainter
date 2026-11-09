// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialProjectionParameters.hh"
#include "NativeMaterialProjectionSource.hh"
#include "FillGizmo.hh"
#include "NativeMaterialProjectionOracle.hh"
#include "NativeMaterialProjectionWitnessTest.hh"
#include <cassert>
#include <QGuiApplication>
using namespace bts;
const auto &field(const GpuLayerParameters &p,std::string_view name){for(std::size_t i=0;i<std::size(GpuLayerParameters::fields);++i)if(name==GpuLayerParameters::fields[i].name)return p.values[i];throw std::logic_error("Missing test parameter");}
int main(int argc,char **argv){
 QGuiApplication app(argc,argv);verify_projection_resource_witness();TextureSet set;set.native_per_uv={3,5};set.cm_per_native=2;
 FillSurfaceAtlas atlas;atlas.resolution=17;atlas.height=9;atlas.extent=4;atlas.center={1,2,3};
 FillProjection fill;auto identity=native_material_projection(fill,&atlas,set);assert(identity->identity_uv);auto rows=identity->encoded();assert(rows[0][0]==1&&rows[0][1]==0&&rows[0][2]==1);
 fill.linked=false;fill.scale={.7,1.3};fill.offset={.12,-.2};fill.rotation=23;fill.scale3={1.2,.8,1.4};fill.offset3={.1,-.2,.3};fill.rotation3={11,-17,7};fill.custom_size_cm={12,20,31};fill.physical=true;
 for(int mode=0;mode<=6;++mode)for(int wrap=0;wrap<=3;++wrap){fill.mode=mode;fill.wrap=wrap;const auto p=native_material_projection(fill,&atlas,set);assert(!p->identity_uv);assert(p->encoded()[0][1]==mode);assert(field(p->parameters,"projection_wrap")[0]==wrap);assert(p->row_count()==(mode==5?NativeMaterialProjection::row_capacity:NativeMaterialProjection::base_rows));if(mode==5){assert(field(p->parameters,"warp_info")[0]==3&&field(p->parameters,"warp_identity")[0]==1);const auto grid=std::make_shared<WarpGrid>();grid->nodes[4].position[1]+=.2;fill.warp=grid;assert(field(native_material_projection(fill,&atlas,set)->parameters,"warp_identity")[0]==0);fill.warp.reset();}}
 fill.mode=6;fill.wrap=0;const auto match=native_material_projection(fill,&atlas,set)->encoded();fill.scale={9,-3};fill.offset={-14,27};fill.rotation=179;fill.offset3={3,4,5};fill.rotation3={23,47,91};fill.scale3={2,3,4};fill.physical=false;fill.filtering=2;assert(native_material_projection(fill,nullptr,set)->encoded()==match);
 fill.mode=1;bool missing=false;try{native_material_projection(fill,nullptr,set);}catch(const std::invalid_argument &){missing=true;}assert(missing);
 auto invalid=*identity;invalid.parameters.at("map_u")[0]=std::numeric_limits<float>::infinity();bool nonfinite=false;try{invalid.encoded();}catch(const std::invalid_argument &){nonfinite=true;}assert(nonfinite);
 const auto source=native_material_projection_source();assert(source.find("texelFetch(layer_parameters_img")==std::string::npos);assert(source.find("bts_fill_warp_project")!=std::string::npos);assert(source.find("native_projection[")!=std::string::npos);assert(source.find("bts_uv=vec3(uv,0)")!=std::string::npos);
 {
  FillProjection p;p.mode=1;p.hardness=0;
  const auto planes=projection_oracle::samples(p,{.2,.3,.4},{.6,0,.8},{},false,1001,2,{1,1},1);
  double value=0;for(const auto &plane:planes)value+=(plane.point[0]*plane.point[0]+.3*plane.point[1]+.1*plane.point[2])*plane.weight;
  assert(std::abs(value-.6039285714285714)<1e-12);
  const auto mean=projection_oracle::coordinate(planes);const double wrong=mean[0]*mean[0]+.3*mean[1]+.1*mean[2];assert(std::abs(value-wrong)>.0001);
 }
 // Real pointer gesture math uses the very same projected paths as drawing/hit.
 const std::array<float,3> lo{-1,-1,-1},hi{1,1,1};
 const auto scene=[](QVector3D p){return QPointF(300+90*p.x()+35*p.y(),300-90*p.z()-25*p.y());};const auto uv=[](QPointF p){return QPointF(100+400*p.x(),500-400*p.y());};
 for(int mode:{0,1,2,3,4})for(int tool=0;tool<3;++tool)for(bool mirrored:{false,true}){FillProjection p;p.mode=mode;p.gizmo=tool;p.linked=false;if(mirrored){p.scale[0]=-1;p.scale3[0]=-1;}const auto geometry=fill_gizmo_geometry(p,lo,hi,{0,0,800,800},scene,uv);FillGizmo gizmo;const auto witness=gizmo.diagnostic(geometry);assert(!witness.isEmpty());const auto from=witness["from"].toArray(),to=witness["to"].toArray();const QPointF first(from[0].toDouble(),from[1].toDouble()),last(to[0].toDouble(),to[1].toDouble());int starts=0,ends=0,changes=0;bool cancelled=false;FillProjection changed=p;gizmo.started=[&]{++starts;};gizmo.changed=[&](const FillProjection &value){++changes;changed=value;};gizmo.finished=[&](bool cancel){++ends;cancelled=cancel;};QMouseEvent press(QEvent::MouseButtonPress,first,first,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);assert(gizmo.press(&press,p,geometry));QMouseEvent move(QEvent::MouseMove,last,last,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);assert(gizmo.move(&move));QMouseEvent release(QEvent::MouseButtonRelease,last,last,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);assert(gizmo.release(&release));assert(starts==1&&ends==1&&changes==1&&!cancelled);assert(mode==0?(changed.offset!=p.offset||changed.rotation!=p.rotation||changed.scale!=p.scale):(changed.offset3!=p.offset3||changed.rotation3!=p.rotation3||changed.scale3!=p.scale3));if(mirrored&&tool==2)assert(changed.scale[0]<0&&changed.scale3[0]<0);assert(gizmo.press(&press,p,geometry));gizmo.finish(true);assert(cancelled&&starts==2&&ends==2);}
}
