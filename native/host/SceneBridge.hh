#pragma once
#include "SceneCamera.hh"
#include <memory>
#include <string>
#include <vector>
class QTemporaryDir;
namespace bts {
struct ConvertedScene {
  std::unique_ptr<QTemporaryDir> directory;
  std::string obj_path;
  std::vector<SceneCamera> cameras;
  std::vector<std::string> embedded_images;
  ConvertedScene();~ConvertedScene();
};
std::unique_ptr<ConvertedScene> convert_mesh_scene(const std::string &,bool import_cameras);
std::unique_ptr<ConvertedScene> convert_collada_scene(const std::string &,bool import_cameras);
}
