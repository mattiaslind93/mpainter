// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include "ProjectFileSafety.hh"
#include "AssetCatalog.hh"
#include "NativeMaterialHeightProject.hh"
#include "NativeMaterialHeightContext.hh"
#include <memory>
#include <atomic>
#include <exception>
#include <functional>
#include <string>
namespace blender {
struct Main;
struct Scene;
struct BlendFileData;
struct View3D;
}  // namespace blender
namespace bts {
class GpuProjectReadback;
class MaterialInspection;
// Owns a detached Blender ID graph. No references to the live Main survive
// snapshot construction; only immutable/COW document and resource data is shared.
class ProjectSaveSnapshot {
  struct Impl;
  std::unique_ptr<Impl> impl_;

 public:
  ProjectSaveSnapshot(blender::Main &,
                      blender::Scene &,
                      blender::View3D &,
                      const TextureDocument &,
                      const std::vector<StudioAsset> &);
  ~ProjectSaveSnapshot();
  bool capture_authored(MaterialInspection &, GpuProjectReadback &);
  ProjectFileIdentity write(const std::string &,
                            const GpuProjectReadback &,
                            std::optional<ProjectFileIdentity> expected = {});
};
void pack_project_environment(blender::Main &, blender::Scene &, blender::View3D &);
ProjectFileIdentity write_project(blender::Main &,
                                  blender::Scene &,
                                  const TextureDocument &,
                                  const std::string &path,
                                  bool update_main_path = true,
                                  std::optional<ProjectFileIdentity> expected = {});
struct PendingProject {
  PendingProject() = default;
  ProjectFileIdentity disk_identity;
  PendingProject(const PendingProject &) = delete;
  PendingProject &operator=(const PendingProject &) = delete;
  blender::BlendFileData *file = nullptr;
  std::optional<TextureDocument> document;
  std::vector<StudioAsset> resources;
  std::exception_ptr failure;
  QByteArray environment_bytes;
  QString environment_name;
  // Worker-created CPU state only. The owner moves this into its explicit
  // context-bound staging holder BEFORE any GPU work; Pending never owns GPU.
  std::unique_ptr<MaterialInspection> validated_inspection;
  bool layer_sources = false;  // validated original-backed map representation
  std::vector<NativeHeightProjectSource> native_sources;
  std::map<std::uint32_t, NativeMaterialGpuContext> native_object_contexts;
  ~PendingProject();
  void validate() const;
  void activate_environment();  // owner thread, before retiring the live Main
  // Always returns ownership, even on failure/cancel, so detached WM/ID cleanup
  // occurs on the owner rather than running native/Python cleanup on a worker.
  static std::unique_ptr<PendingProject> prepare(
      const std::string &path,
      const std::shared_ptr<std::atomic_bool> &cancel = {},
      const std::function<void(const QString &)> &progress = {});
  static std::unique_ptr<PendingProject> read(const std::string &path);
};
}  // namespace bts
