#pragma once
// Explicit diagnostic ownership of three canonical inputs. No ordinary caller
// acquires this lease; without a live record the shader attach path is unchanged.
#include "BKE_main.hh"
#include "BLI_listbase.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "GPU_context.hh"
#include <array>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <thread>

namespace bts {
class MaterialLayerReferenceLease final {
  blender::Main *main_;
  blender::Material *material_;
  blender::bNodeTree *tree_;
  blender::bNode *principled_;
  std::array<blender::bNodeSocket *,3> sockets_;
  blender::GPUContext *context_ = blender::GPU_context_active_get();
  std::thread::id owner_ = std::this_thread::get_id();
  bool released_ = false;
  inline static thread_local MaterialLayerReferenceLease *active_ = nullptr;

  static void require(bool value, const char *message)
  {
    if (!value) throw std::logic_error(message);
  }
  void check() const
  {
    require(owner_ == std::this_thread::get_id() && context_ &&
                blender::GPU_context_active_get() == context_,
            "Surface input lease escaped owner GPU context");
    require(!released_ && active_ == this && material_->nodetree == tree_,
            "Surface input lease Main/material/tree ownership changed");
    bool material_found = false, node_found = false;
    for (const auto &value : main_->materials) material_found |= &value == material_;
    for (const auto &value : tree_->nodes) node_found |= &value == principled_;
    require(material_found && node_found,
            "Surface input lease lost retained Main/material/Principled");
    for (auto *socket : sockets_) {
      bool found = false;
      for (const auto &value : principled_->inputs) found |= &value == socket;
      require(found, "Surface input lease socket identity changed");
    }
  }

 public:
  MaterialLayerReferenceLease(blender::Main &main, blender::Material &material,
                             blender::bNode &principled,
                             std::array<blender::bNodeSocket *,3> sockets)
      : main_(&main), material_(&material), tree_(material.nodetree),
        principled_(&principled), sockets_(sockets)
  {
    const char *flag = std::getenv("BTS_PD149_SURFACE_TEST");
    require(flag && std::strcmp(flag, "1") == 0,
            "Surface input lease requires explicit diagnostic mode");
    require(!active_, "Nested or foreign surface input lease");
    require(tree_ && std::strcmp(principled.idname, "ShaderNodeBsdfPrincipled") == 0,
            "Surface input lease requires retained canonical Principled tree");
    const char *names[]{"Base Color", "Metallic", "Roughness"};
    for (int i = 0; i < 3; ++i) {
      require(sockets_[i] && std::strcmp(sockets_[i]->identifier, names[i]) == 0,
              "Surface input lease target socket differs");
    }
    active_ = this;
    try { check(); }
    catch (...) { active_ = nullptr; throw; }
  }
  // Called only at the three final layered Color/Metal/Rough attachments.
  // Other materials and Normal always retain their ordinary binding behavior.
  static bool owns(blender::Main &main, blender::Material &material,
                   blender::bNode &principled, const char *socket_identifier)
  {
    if (!active_ || active_->material_ != &material) return false;
    active_->check();
    require(active_->main_ == &main && active_->tree_ == material.nodetree &&
                active_->principled_ == &principled,
            "Surface input lease queried with foreign owner identities");
    for (auto *owned : active_->sockets_)
      if (std::strcmp(owned->identifier, socket_identifier) == 0) return true;
    return false;
  }
  // Reference restoration and its exact graph/default checks must precede this.
  void release()
  {
    check();
    active_ = nullptr;
    released_ = true;
  }
  ~MaterialLayerReferenceLease()
  {
    if (!released_) std::terminate();
  }
  MaterialLayerReferenceLease(const MaterialLayerReferenceLease &) = delete;
  MaterialLayerReferenceLease &operator=(const MaterialLayerReferenceLease &) = delete;
  MaterialLayerReferenceLease(MaterialLayerReferenceLease &&) = delete;
  MaterialLayerReferenceLease &operator=(MaterialLayerReferenceLease &&) = delete;
};
}
