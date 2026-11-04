#include "OrbitCamera.hh"
#include <QQuaternion>
#include <QVector4D>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
using namespace bts;
namespace {
constexpr float pi=std::numbers::pi_v<float>;
double matrix_error(const QMatrix4x4 &a,const QMatrix4x4 &b){double error=0;for(int i=0;i<16;++i)error=std::max(error,double(std::abs(a.constData()[i]-b.constData()[i])));return error;}
QPointF logical(QPoint p,QRect rect,double dpr=1){return {(rect.x()+p.x())/dpr,(rect.y()+rect.height()-1-p.y())/dpr};}
// Independent adapter transcribed from the pinned Blender float operations.
// Qt's quaternion product/matrix conversion is separate from production math.
struct InclusiveRegion {int xmin,xmax,ymin,ymax;};
// Independent Blender rcti operations on explicit endpoints. Window pixel counts
// come from size_x+1 in area.cc; this reference never calls production vector().
QVector3D reference_vector(QPoint point,InclusiveRegion rect){const float w=rect.xmax-rect.xmin,h=rect.ymax-rect.ymin,m=std::min(w,h);const float x=(point.x()-(rect.xmin+rect.xmax)/2)/((w*(m/w))/2),y=(point.y()-(rect.ymin+rect.ymax)/2)/((h*(m/h))/2);const float d=std::sqrt(x*x+y*y),t=1.1f/std::sqrt(2.f);return {x,y,d<t?std::sqrt(1.1f*1.1f-d*d):t*t/d};}
QVector3D reference_vector(QPoint point,QSize pixels){return reference_vector(point,InclusiveRegion{0,pixels.width()-1,0,pixels.height()-1});}
QMatrix4x4 reference(QPoint a,QPoint b,QSize size,const QMatrix4x4 &initial,bool reverse=false){
 const auto start=reference_vector(a,size),end=reference_vector(b,size);const auto axis=QVector3D::crossProduct(start,end);float angle=(end-start).length()/(2*1.1f)*pi*1.5f;angle=(angle+pi)-(2*pi)*std::floor((angle+pi)/(2*pi))-pi;
 QQuaternion delta;if(axis.lengthSquared()!=0){const auto n=axis.normalized();const float sine=std::sin(angle/2);delta=QQuaternion(std::cos(angle/2),n.x()*sine,n.y()*sine,n.z()*sine);}
 QMatrix3x3 rows;for(int i=0;i<3;++i)for(int j=0;j<3;++j)rows(i,j)=initial(i,j);const auto prior=QQuaternion::fromRotationMatrix(rows);const auto result=(reverse?prior*delta:delta*prior).normalized().toRotationMatrix();auto matrix=initial;for(int i=0;i<3;++i)for(int j=0;j<3;++j)matrix(i,j)=result(i,j);return matrix;
}
void rotation_only_equal(const QMatrix4x4 &a,const QMatrix4x4 &b,double tolerance=2e-6){for(int i=0;i<3;++i)for(int j=0;j<3;++j)assert(std::abs(a(i,j)-b(i,j))<tolerance);}
void analytic_and_reference(){
 const auto center=trackball::vector({100,100},{201,201});assert(center[0]==0&&center[1]==0&&std::abs(center[2]-1.1f)<1e-7);
 const auto outside=trackball::vector({300,100},{201,201});assert(outside[0]==2&&outside[1]==0&&std::abs(outside[2]-.3025f)<1e-7);
 // Even pixel counts expose the old incorrect center; odd rectangular counts
 // expose the old pixel-count denominator even when the center happened to agree.
 const auto even=trackball::vector({99,99},{200,200});assert(even[0]==0&&even[1]==0&&even[2]==1.1f);
 const auto adjacent=trackball::vector({100,99},{200,200});assert(std::abs(adjacent[0]-float(1/99.5))<1e-8&&adjacent[1]==0);
 const auto rectangle=trackball::vector({600,375},{901,601});assert(rectangle[0]==.5f&&rectangle[1]==.25f&&std::abs(rectangle[2]-std::sqrt(1.1f*1.1f-.3125f))<1e-7);
 for(const InclusiveRegion rect:{InclusiveRegion{17,316,29,228},InclusiveRegion{17,317,29,229}}){
  const QPoint absolute((rect.xmin+rect.xmax)/2+37,(rect.ymin+rect.ymax)/2-21);
  const auto wanted=reference_vector(absolute,rect);const auto actual=trackball::vector(absolute-QPoint(rect.xmin,rect.ymin),QSize(rect.xmax-rect.xmin+1,rect.ymax-rect.ymin+1));
  for(int i=0;i<3;++i)assert(std::abs(actual[i]-wanted[i])<1e-7);
 }
 assert(!trackball::pointer({0,0},QRect(0,0,1,200),1)&&!trackball::pointer({0,0},QRect(0,0,200,1),1));
 OrbitCamera degenerate;assert(!degenerate.begin_trackball({0,0},QRect(0,0,1,200),1));
 // Sphere/hyperbola join, outside sphere, a wrapped >pi chord, and zero axis.
 bool wrong_order_detected=false,normalized_vector_detected=false,acos_detected=false;
 for(QSize size:{QSize(200,200),QSize(900,601),QSize(301,901)})for(auto points:{std::pair(QPoint(100,100),QPoint(100,100)),std::pair(QPoint(100,100),QPoint(178,100)),std::pair(QPoint(12,31),QPoint(280,191)),std::pair(QPoint(-900,31),QPoint(1400,1700))}){
  OrbitCamera camera;const auto initial=camera.view_matrix();const QRect rect(QPoint(23,51),size);const auto press=camera.begin_trackball(logical(points.first,rect),rect,1);assert(press);assert(camera.orbit_trackball(*press,logical(points.second,rect)));const auto expected=reference(points.first,points.second,size,initial);rotation_only_equal(camera.view_matrix(),expected,8e-6);wrong_order_detected|=matrix_error(expected,reference(points.first,points.second,size,initial,true))>1e-2;
  const auto a=reference_vector(points.first,size),b=reference_vector(points.second,size);const float chord=(b-a).length()/(2*1.1f)*pi*1.5f;const float normalized=(b.normalized()-a.normalized()).length()/(2*1.1f)*pi*1.5f;normalized_vector_detected|=std::abs(chord-normalized)>.02f;acos_detected|=std::abs(chord-std::acos(std::clamp(QVector3D::dotProduct(a.normalized(),b.normalized()),-1.f,1.f)))>.02f;
 }
 assert(wrong_order_detected&&normalized_vector_detected&&acos_detected);
 // Integer center and physical pointer convention, including offset odd panes.
 for(QRect rect:{QRect(0,0,300,200),QRect(17,29,301,201),QRect(41,53,201,301)})for(double dpr:{1.,2.}){
  const QPoint point((rect.width()-1)/2,(rect.height()-1)/2);assert(trackball::pointer(logical(point,rect,dpr),rect,dpr)==point);const auto v=trackball::vector(point,rect.size());assert(v[0]==0&&v[1]==0&&v[2]==1.1f);
  OrbitCamera one,two;const QPoint end=point+QPoint(55,-33);auto p1=one.begin_trackball(logical(point,rect),rect,1),p2=two.begin_trackball(logical(point,rect,dpr),rect,dpr);assert(p1&&p2);assert(one.orbit_trackball(*p1,logical(end,rect)));assert(two.orbit_trackball(*p2,logical(end,rect,dpr)));assert(matrix_error(one.view_matrix(),two.view_matrix())<1e-6);
 }
 // Same shorter side and relative physical points across aspect ratios.
 OrbitCamera landscape,portrait;const QRect a(0,0,800,400),b(0,0,400,800);auto pa=landscape.begin_trackball(logical({350,230},a),a,1),pb=portrait.begin_trackball(logical({150,430},b),b,1);assert(pa&&pb);landscape.orbit_trackball(*pa,logical({440,190},a));portrait.orbit_trackball(*pb,logical({240,390},b));rotation_only_equal(landscape.view_matrix(),portrait.view_matrix());
}
void gestures_and_camera(){
 const QRect pane(31,47,901,601);const QPoint from(210,390),to(680,120);OrbitCamera one,many;const auto before=one.view_matrix();auto p=one.begin_trackball(logical(from,pane),pane,1);assert(p);const auto pivot=one.center();const auto distance=one.distance(),extent=one.vertical_extent();const auto projection=one.projection_matrix(pane.size());
 assert(one.orbit_trackball(*p,logical(to,pane)));for(int i=1;i<=31;++i)assert(many.orbit_trackball(*p,logical(from+(to-from)*i/31,pane)));assert(matrix_error(one.view_matrix(),many.view_matrix())==0);assert(one.center()==pivot&&one.distance()==distance&&one.vertical_extent()==extent&&one.projection_matrix(pane.size())==projection);
 // A second drag composes from the current, freely rolled orientation.
 const auto first=one.view_matrix();auto second=one.begin_trackball(logical(to,pane),pane,1);assert(second);one.orbit_trackball(*second,logical({400,500},pane));rotation_only_equal(one.view_matrix(),reference(to,{400,500},pane.size(),first),8e-6);
 const auto held=one.view_matrix();assert(!one.begin_trackball({},QRect(),1));assert(!one.begin_trackball({},pane,0));assert(!one.begin_trackball({NAN,0},pane,1));assert(!one.orbit_trackball(*p,{INFINITY,0}));assert(one.view_matrix()==held);
 // Zero-delta turntable rebase keeps roll and the visible matrix.
 one.orbit({},false);assert(matrix_error(held,one.view_matrix())<2e-6);one.orbit({10,20},false);assert(std::isfinite(one.yaw_degrees())&&std::isfinite(one.pitch_degrees()));
 OrbitCamera legacy;legacy.orbit({10,20},false);assert(legacy.yaw_degrees()==21.5&&legacy.pitch_degrees()==25);legacy.orbit({1,1},true);assert(std::remainder(legacy.yaw_degrees(),15)==0&&std::remainder(legacy.pitch_degrees(),15)==0);
 // Pan follows the same rolled rows consumed by render and inverse picking.
 const auto view=many.view_matrix();const auto center=many.center();const auto units=many.vertical_extent()/pane.height();many.pan({20,-10},pane.height());QVector3D expected;for(int i=0;i<3;++i)expected[i]=center[i]+float((-view(0,i)*20-view(1,i)*10)*units);assert((many.center()-expected).length()<1e-6);
 const auto rotation=many.view_matrix();assert(many.frame_bounds({-1,-2,-3},{2,3,4},pane.size()));rotation_only_equal(rotation,many.view_matrix());
 for(bool ortho:{false,true}){many.set_orthographic(ortho);const auto vp=many.projection_matrix(pane.size())*many.view_matrix();bool valid=false;const auto inverse=vp.inverted(&valid);assert(valid);for(QVector3D point:{many.center(),many.center()+QVector3D(.01,.02,.03)}){const auto clip=vp*QVector4D(point,1);const auto ndc=clip.toVector3DAffine();const auto recovered=(inverse*QVector4D(ndc,1)).toVector3DAffine();assert((point-recovered).length()<.003);}}
 // Imported roll/poles detach without changing view or gate footprint.
 for(auto up:{std::array<float,3>{1,0,0},std::array<float,3>{0,1,0}}){OrbitCamera camera;SceneCamera source;source.position={0,0,5};source.forward={0,0,-1};source.up=up;source.vertical_fov=35;source.aspect=2;assert(camera.adopt_scene_camera(source));const auto v=camera.view_matrix(),proj=camera.projection_matrix({600,600});camera.release_scene_camera({600,600});assert(matrix_error(v,camera.view_matrix())==0);const auto detached=camera.projection_matrix({600,600});assert(std::abs(proj(0,0)-detached(0,0))<1e-6&&std::abs(proj(1,1)-detached(1,1))<1e-6);const QRect rect(0,0,600,600);auto press=camera.begin_trackball(logical({200,400},rect),rect,1);assert(press);camera.orbit_trackball(*press,logical({260,340},rect));const auto rolled=camera.view_matrix();camera.orbit({},false);assert(matrix_error(rolled,camera.view_matrix())<2e-6);}
 // Long sequences retain an orthonormal, right-handed world-to-view basis.
 for(int i=0;i<1000;++i){auto press=many.begin_trackball(logical({200,300},pane),pane,1);assert(press);many.orbit_trackball(*press,logical({210+i%7,280-i%5},pane));}const auto m=many.view_matrix();for(int i=0;i<3;++i){const auto row=m.row(i).toVector3D();assert(std::abs(row.length()-1)<1e-6);for(int j=i+1;j<3;++j)assert(std::abs(QVector3D::dotProduct(row,m.row(j).toVector3D()))<1e-6);}assert(QVector3D::dotProduct(QVector3D::crossProduct(m.row(0).toVector3D(),m.row(1).toVector3D()),m.row(2).toVector3D())>.99999);
}
}
int main(){analytic_and_reference();gestures_and_camera();std::cout<<"Trackball pinned Blender math, rectangular DPR gestures, free roll, turntable bridge, camera gate and projection/picking PASS\n";}
