// SPDX-License-Identifier: GPL-3.0-or-later
#include "OrbitCamera.hh"
#include <QVector4D>
#include <cassert>
#include <cmath>
#include <iostream>
namespace {
void clipping_test(){
  using namespace bts;
  OrbitCamera camera;
  assert(camera.clipping_range()==std::pair(.01,1000.));
  assert(camera.set_clipping({.005,500}));
  assert(camera.frame_bounds({-1,0,-1},{1,0,1},{900,600}));
  assert(camera.clipping_range()==std::pair(.005,500.));
  const auto before=camera.projection_matrix({900,600});
  for(ViewportClipping invalid: {ViewportClipping{0,10},ViewportClipping{-1,10},ViewportClipping{10,10},ViewportClipping{11,10},ViewportClipping{1,INFINITY},ViewportClipping{NAN,10}}){
    assert(!camera.set_clipping(invalid));assert(camera.projection_matrix({900,600})==before);
  }
  camera.orbit({camera.yaw_degrees()/.35,-camera.pitch_degrees()/.35},false);
  assert(camera.frame_bounds({-1,0,-1},{1,0,1},{900,600}));
  camera.dolly(std::log(.02/camera.distance())*100);
  assert(std::abs(camera.distance()-.02)<1e-10);
  auto ndc=[&](QVector3D world){return (camera.projection_matrix({900,600})*camera.view_matrix()*QVector4D(world,1)).toVector3DAffine();};
  assert(std::abs(ndc({0,0,0}).z())<1);
  assert(camera.set_clipping({.1,1000}));assert(ndc({0,0,0}).z()<-1);
  assert(camera.set_clipping({.01,1000}));assert(std::abs(ndc({0,0,0}).z())<1);
  const auto m=camera.projection_matrix({900,600})*camera.view_matrix();bool invertible=false;const auto inverse=m.inverted(&invertible);assert(invertible);
  const QVector4D point(.001,0,.002,1);
  // Homogeneous divide and inverse are the same path used by viewport picking.
  const auto clip=m*point;const auto roundtrip=(inverse*QVector4D(clip.toVector3DAffine(),1)).toVector3DAffine();
  assert((roundtrip-point.toVector3D()).length()<1e-5);
  camera.set_orthographic(true);assert(camera.clipping_range()==std::pair(-1000.,1000.));
  assert(std::abs(ndc({0,0,0}).z())<1);assert(camera.set_clipping({.000001,10}));assert(camera.clipping_range()==std::pair(-10.,10.));
  camera.set_orthographic(false);camera.dolly(-1000);assert(camera.distance()<.00001); // No .01 zoom clamp.
  assert(camera.frame_bounds({-1e6,-1e6,-1e6},{1e6,1e6,1e6},{900,600}));assert(camera.clipping_range()==std::pair(.000001,10.));
}
}
int main(){using namespace bts;clipping_test();OrbitCamera camera;assert(camera.frame_bounds({-1,0,-1},{1,0,1},{900,600}));SceneCamera source;source.name="FrontReference";source.position={0,-4,0};source.forward={0,1,0};source.up={0,0,1};source.vertical_fov=45;source.aspect=1.5;source.near_clip=.1;source.far_clip=100;const auto immutable=source;assert(camera.adopt_scene_camera(source));auto eye=camera.view_matrix()*QVector4D(0,-4,0,1);assert(eye.toVector3D().length()<1e-5);auto center=camera.projection_matrix({900,600})*camera.view_matrix()*QVector4D(0,0,0,1);assert(std::abs(center.x())<1e-6&&std::abs(center.y())<1e-6);const auto projection=camera.projection_matrix({600,600});assert(std::abs(projection(1,1)-1/(std::tan(3.141592653589793/8)*1.5))<1e-5);assert(camera.clipping_range()==std::pair(double(source.near_clip),double(source.far_clip)));assert(camera.set_clipping({.002,2000}));assert(camera.clipping_range().first==source.near_clip);const auto view=camera.view_matrix();camera.release_scene_camera({600,600});assert(camera.view_matrix()==view);assert(camera.clipping_range()==std::pair(.002,2000.));const auto released=camera.projection_matrix({600,600});assert(std::abs(released(0,0)-projection(0,0))<1e-6&&std::abs(released(1,1)-projection(1,1))<1e-6);const auto distance=camera.distance();camera.zoom_wheel(120);assert(camera.distance()<distance&&source==immutable);SceneCamera rolled=source;rolled.up={1,0,0};assert(camera.adopt_scene_camera(rolled));const auto right=camera.view_matrix()*QVector4D(0,0,-1,0);assert(std::abs(right.x()-1)<1e-5&&std::abs(right.y())<1e-5);SceneCamera angle=source;angle.position={3,-4,1};angle.forward={-3,4,-1};angle.vertical_fov=35;angle.aspect=16.f/9;assert(camera.adopt_scene_camera(angle));eye=camera.view_matrix()*QVector4D(3,-4,1,1);assert(eye.toVector3D().length()<1e-5&&camera.fov_degrees()==35);auto invalid=angle;invalid.forward={0,0,0};const auto previous=camera.view_matrix();assert(!camera.adopt_scene_camera(invalid)&&camera.view_matrix()==previous);std::cout<<"Scene-camera pose, roll, FOV/aspect gate, immutable navigation handoff and invalid-pose transaction passed\n";}
