#pragma once
#include <memory>
#include <string>
#include <QJsonObject>
namespace blender { struct Main; struct Scene; struct View3D; }
namespace bts {
// Prepared on the owner thread before replacing a document. No user preferences are written.
class NewProjectProfile {
 public:
  static std::unique_ptr<NewProjectProfile> prepare(const std::string &hdr_path,
                                                    blender::Scene &, blender::View3D &);
  ~NewProjectProfile();
  // Owner-only initialization, before worker/program use; does not change view or lighting.
  static void initialize_working_space(blender::Main &);
  // Upgrade the unversioned Studio default once; preserve authored rotations/alignment.
  static void restore_environment_orientation(blender::Scene &, blender::View3D &);
  void apply(blender::Main &, blender::Scene &, blender::View3D &);
  static QJsonObject inspect(blender::Main &, blender::Scene &, blender::View3D &);
 private:
  struct Impl;
  explicit NewProjectProfile(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
}
