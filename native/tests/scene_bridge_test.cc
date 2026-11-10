// SPDX-License-Identifier: GPL-3.0-or-later
#include "SceneBridge.hh"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cassert>
#include <cmath>
#include <iostream>
int main()
{
  using namespace bts;
  QTemporaryDir folder(QDir::currentPath() + "/camera-fixture-XXXXXX");
  assert(folder.isValid());
  const auto path = folder.path() + "/card.dae";
  QFile file(path);
  assert(file.open(QIODevice::WriteOnly));
  const QByteArray fixture = R"BTSDAE(<?xml version="1.0" encoding="utf-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
 <asset><created>2026-10-18T00:00:00Z</created><modified>2026-10-18T00:00:00Z</modified><unit name="meter" meter="1"/><up_axis>Y_UP</up_axis></asset>
 <library_effects><effect id="effect"><profile_COMMON><technique sid="common"><lambert><diffuse><color>0.5 0.5 0.5 1</color></diffuse></lambert></technique></profile_COMMON></effect></library_effects>
 <library_materials><material id="CardSurface" name="CardSurface"><instance_effect url="#effect"/></material></library_materials>
 <library_geometries><geometry id="card" name="Card"><mesh>
  <source id="pos"><float_array id="pos-array" count="12">-1 -1 0 1 -1 0 1 1 0 -1 1 0</float_array><technique_common><accessor source="#pos-array" count="4" stride="3"><param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>
  <source id="uv"><float_array id="uv-array" count="8">0 0 1 0 1 1 0 1</float_array><technique_common><accessor source="#uv-array" count="4" stride="2"><param name="S" type="float"/><param name="T" type="float"/></accessor></technique_common></source>
  <vertices id="verts"><input semantic="POSITION" source="#pos"/></vertices>
  <triangles count="2" material="mat"><input semantic="VERTEX" source="#verts" offset="0"/><input semantic="TEXCOORD" source="#uv" offset="1" set="0"/><p>0 0 1 1 2 2 0 0 2 2 3 3</p></triangles>
 </mesh></geometry></library_geometries>
 <library_cameras>
  <camera id="FrontReference" name="FrontReference"><optics><technique_common><perspective><yfov>45</yfov><aspect_ratio>1.5</aspect_ratio><znear>0.1</znear><zfar>100</zfar></perspective></technique_common></optics></camera>
  <camera id="AngleReference" name="AngleReference"><optics><technique_common><perspective><yfov>35</yfov><aspect_ratio>1.7777778</aspect_ratio><znear>0.1</znear><zfar>100</zfar></perspective></technique_common></optics></camera>
 </library_cameras>
 <library_visual_scenes><visual_scene id="Scene" name="Scene">
  <node id="CardNode" name="Card"><instance_geometry url="#card"><bind_material><technique_common><instance_material symbol="mat" target="#CardSurface"/></technique_common></bind_material></instance_geometry></node>
  <node id="FrontCameraNode" name="FrontReference"><lookat>0 0 4 0 0 0 0 1 0</lookat><instance_camera url="#FrontReference"/></node>
  <node id="AngleCameraNode" name="AngleReference"><lookat>3 1 4 0 0 0 0 1 0</lookat><instance_camera url="#AngleReference"/></node>
 </visual_scene></library_visual_scenes>
 <scene><instance_visual_scene url="#Scene"/></scene>
</COLLADA>
)BTSDAE";
  assert(file.write(fixture) == fixture.size());
  file.close();
  auto scene = convert_collada_scene(path.toStdString(), true);
  assert(scene->cameras.size() == 2 && QFileInfo::exists(QString::fromStdString(scene->obj_path)));
  const auto &front = scene->cameras[0], &angle = scene->cameras[1];
  assert(front.name == "FrontReference" && angle.name == "AngleReference");
  assert(std::abs(front.position[1] + 4) < 1e-5 && std::abs(front.position[2]) < 1e-5);
  assert(std::abs(front.vertical_fov - 45) < 1e-4 && std::abs(angle.vertical_fov - 35) < 1e-4);
  assert(std::abs(front.aspect - 1.5) < 1e-5 && std::abs(angle.aspect - 16.f / 9) < 1e-5);
  auto none = convert_collada_scene(path.toStdString(), false);
  assert(none->cameras.empty());
  assert(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  auto centimeters = fixture;
  centimeters.replace("meter=\"1\"", "meter=\"0.01\"");
  file.write(centimeters);
  file.close();
  auto scaled = convert_collada_scene(path.toStdString(), true);
  assert(std::abs(scaled->cameras[0].position[1] + .04) < 1e-5 &&
         std::abs(scaled->cameras[0].near_clip - front.near_clip * .01) < 1e-6);
  std::cout
      << "In-process Collada bridge: native-axis camera names, calibrated FOV, camera exclusion and scene-unit scaling passed\n";
}
