// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Test only: SAME canonical Principled, with independently computed uniform
// Color*AO/Metal/Rough constants. No Emission, view branch or scene changes.
// Main/material/tree must outlive this scope; restore before history/Save/Open.
#include "MaterialLayeringGpuTap.hh"  // Existing pointer/default graph signature.
#include "MaterialLayerReferenceLease.hh"
#include <optional>
#include "BKE_main.hh"
#include "DEG_depsgraph.hh"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <thread>
#include <vector>

namespace bts::pd149_surface_test {

struct UniformInput {
  std::array<double, 3> color;
  double metallic, roughness, ao;
};
// Binary-exact recipe, expressed independently of the document/layering model.
inline constexpr std::array<UniformInput, 4> uniform_inputs{{{{{.25, .5, .75}}, 0, .125, 1},
                                                             {{{.75, .25, .5}}, .25, .25, .5},
                                                             {{{.125, .75, .25}}, .5, .5, .75},
                                                             {{{.5, .125, .875}}, 1, .75, .25}}};
inline constexpr std::array<double, 3> uniform_masks{.25, .5, .125};
inline constexpr std::array<double, 4> uniform_weights{.328125, .109375, .4375, .125};
inline constexpr double uniform_ao_intensity = .5;

struct UniformReference {
  std::array<double, 4> weights{};
  std::array<double, 3> color_before_ao{}, color{};
  double metallic = 0, roughness = 0, ao = 0, ao_multiplier = 0;
};

// Caller supplies known recipe masks after real edits; never read shader/AOV
// weights or call production layered_material_weights/conversion builders.
inline UniformReference reference(std::array<double, 3> masks = uniform_masks,
                                  double ao_intensity = uniform_ao_intensity)
{
  for (double value : masks)
    pd149_test::require(std::isfinite(value) && value >= 0 && value <= 1,
                        "Surface reference mask outside 0..1");
  pd149_test::require(std::isfinite(ao_intensity) && ao_intensity >= 0 && ao_intensity <= 1,
                      "Surface reference AO intensity outside 0..1");
  UniformReference result;
  // Allocate the final overlay first, then split each remaining contribution.
  result.weights[3] = masks[2];
  const double lower = 1 - masks[2];
  result.weights[2] = lower * masks[1];
  const double first = lower * (1 - masks[1]);
  result.weights[1] = first * masks[0];
  result.weights[0] = first * (1 - masks[0]);
  if (masks == uniform_masks)
    pd149_test::require(result.weights == uniform_weights,
                        "Literal uniform weight recipe differs");
  for (int input = 0; input < 4; ++input) {
    const auto &value = uniform_inputs[input];
    const double weight = result.weights[input];
    for (int component = 0; component < 3; ++component)
      result.color_before_ao[component] += weight * value.color[component];
    result.metallic += weight * value.metallic;
    result.roughness += weight * value.roughness;
    result.ao += weight * value.ao;
  }
  // Geometry AO is neutral one in this bounded fixture. No extra AO product.
  result.ao_multiplier = (1 - ao_intensity) + ao_intensity * result.ao;
  for (int component = 0; component < 3; ++component)
    result.color[component] = result.color_before_ao[component] * result.ao_multiplier;
  return result;
}

class PrincipledReference final {
  using Node = blender::bNode;
  using Socket = blender::bNodeSocket;
  using Link = blender::bNodeLink;
  struct SavedSocket {
    Socket *socket = nullptr;
    void *storage = nullptr;
    std::array<unsigned char, 16> original{}, replacement{};
    std::size_t bytes = 0;
    Link *link = nullptr;
    bool detached = false;
  };
  blender::Main &main_;
  blender::Material &material_;
  blender::bNodeTree *tree_ = nullptr;
  Node *principled_ = nullptr;
  blender::GPUContext *context_ = blender::GPU_context_active_get();
  std::thread::id owner_ = std::this_thread::get_id();
  std::array<SavedSocket, 3> sockets_{};
  std::vector<Link *> order_;
  struct LinkState {
    Link *link;
    Node *from, *to;
    Socket *out, *in;
    int flags, sort;
  };
  std::vector<LinkState> links_;
  QJsonObject before_;
  UniformReference value_;
  bool active_ = false;
  std::optional<MaterialLayerReferenceLease> lease_;

  void owner() const
  {
    pd149_test::require(owner_ == std::this_thread::get_id() && context_ &&
                            blender::GPU_context_active_get() == context_,
                        "Surface reference escaped its owner context");
    pd149_test::require(material_.nodetree == tree_ &&
                            native_principled_surface(material_) == principled_,
                        "Surface reference material/tree/Principled identity changed");
  }
  QJsonObject graph() const
  {
    const auto signature = pd149_test::live_signature(material_);
    QJsonArray sorts;
    for (const auto &link : tree_->links)
      sorts.append(link.multi_input_sort_id);
    return {{"nodes", signature["nodes"]},
            {"links", signature["links"]},
            {"sorts", sorts},
            {"material_flag", signature["material_flag"]},
            {"tree_flag", signature["tree_flag"]}};
  }
  QJsonObject expected() const
  {
    using namespace blender;
    auto result = before_;
    QJsonArray links, sorts;
    const auto old_links = before_["links"].toArray(), old_sorts = before_["sorts"].toArray();
    for (int index = 0; index < old_links.size(); ++index) {
      const auto row = old_links[index].toArray();
      bool removed = false;
      for (const auto &saved : sockets_)
        removed |= row[0].toArray() == pd149_test::pointer(saved.link);
      if (!removed) {
        links.append(row);
        sorts.append(old_sorts[index]);
      }
    }
    result["links"] = links;
    result["sorts"] = sorts;
    auto nodes = result["nodes"].toArray();
    for (int node_index = 0; node_index < nodes.size(); ++node_index) {
      auto node = nodes[node_index].toObject();
      for (const char *direction : {"inputs", "outputs"}) {
        auto sockets = node[direction].toArray();
        for (int socket_index = 0; socket_index < sockets.size(); ++socket_index) {
          auto socket = sockets[socket_index].toObject();
          // Native topology updates recompute this derived flag on BOTH ends of
          // every link, including an upstream output with no remaining consumers.
          bool linked = false;
          for (const auto &entry : links) {
            const auto row = entry.toArray();
            linked |= socket["pointer"].toArray() == row[2].toArray() ||
                      socket["pointer"].toArray() == row[4].toArray();
          }
          socket["flags"] = int((socket["flags"].toInt() & ~SOCK_IS_LINKED) |
                                (linked ? SOCK_IS_LINKED : 0));
          for (int index = 0; index < 3; ++index)
            if (socket["pointer"].toArray() == pd149_test::pointer(sockets_[index].socket)) {
              if (index == 0) {
                bNodeSocketValueRGBA replacement;
                std::memcpy(&replacement, sockets_[0].replacement.data(), sizeof(replacement));
                socket["default"] = QJsonArray{replacement.value[0],
                                               replacement.value[1],
                                               replacement.value[2],
                                               replacement.value[3]};
              }
              else
                socket["default"] = QJsonArray{
                    float(index == 1 ? value_.metallic : value_.roughness)};
            }
          sockets[socket_index] = socket;
        }
        node[direction] = sockets;
      }
      nodes[node_index] = node;
    }
    result["nodes"] = nodes;
    return result;
  }
  void updated()
  {
    BKE_ntree_update_after_single_tree_change(main_, *tree_);
    DEG_id_tag_update_ex(&main_, &material_.id, blender::ID_RECALC_SHADING);
    DEG_id_tag_update_ex(&main_, &tree_->id, blender::ID_RECALC_NTREE_OUTPUT);
  }
  void restore_owned()
  {
    using namespace blender;
    // Original order includes the detached entries: each predecessor is already
    // restored before its successor, including adjacent target input links.
    Link *previous = nullptr;
    for (auto *link : order_) {
      for (auto &saved : sockets_)
        if (saved.link == link && saved.detached) {
          if (previous)
            BLI_insertlinkafter(&tree_->links, previous, link);
          else
            BLI_addhead(&tree_->links, link);
          saved.detached = false;
          BKE_ntree_update_tag_link_added(tree_, link);
        }
      previous = link;
    }
    for (auto &saved : sockets_) {
      std::memcpy(saved.storage, saved.original.data(), saved.bytes);
      BKE_ntree_update_tag_socket_property(tree_, saved.socket);
    }
    updated();
    pd149_test::require(graph() == before_, "Surface reference failed exact graph restoration");
    for (const auto &saved : sockets_)
      pd149_test::require(saved.socket->default_value == saved.storage &&
                              std::memcmp(saved.storage, saved.original.data(), saved.bytes) == 0,
                          "Surface reference failed exact default-byte restoration");
    lease_->release();
    lease_.reset();
    active_ = false;
  }

 public:
  PrincipledReference(blender::Main &main, blender::Material &material, UniformReference value)
      : main_(main), material_(material), value_(value)
  {
    using namespace blender;
    using pd149_test::require;
    require(material.nodetree, "Surface reference requires a material tree");
    tree_ = material.nodetree;
    principled_ = native_principled_surface(material);
    require(principled_, "Surface reference requires SAME canonical Principled");
    owner();
    bool in_main = false;
    for (const auto &entry : main.materials)
      in_main |= &entry == &material;
    require(in_main, "Surface reference material is outside its retained Main");
    if (auto *view = bke::node_find_node_by_name(*tree_, "__BTS_ViewSwitch")) {
      require(STREQ(view->idname, "ShaderNodeMixShader"),
              "Surface reference view wrapper type differs");
      auto &factor = pd149_test::input(*view, "Fac");
      require(factor.type == SOCK_FLOAT && factor.default_value &&
                  static_cast<const bNodeSocketValueFloat *>(factor.default_value)->value == 0,
              "Surface reference must retain actual first branch");
      for (const auto &link : tree_->links)
        require(link.tosock != &factor, "Surface reference view factor unexpectedly linked");
    }
    before_ = graph();
    const char *names[]{"Base Color", "Metallic", "Roughness"};
    for (int index = 0; index < 3; ++index) {
      auto &saved = sockets_[index];
      saved.socket = &pd149_test::input(*principled_, names[index]);
      require(saved.socket->type == (index == 0 ? SOCK_RGBA : SOCK_FLOAT) &&
                  saved.socket->default_value,
              "Surface reference target input type/storage missing");
      saved.storage = saved.socket->default_value;
      saved.bytes = index == 0 ? sizeof(bNodeSocketValueRGBA) : sizeof(bNodeSocketValueFloat);
      require(saved.bytes <= saved.original.size(),
              "Surface reference default storage size differs");
      std::memcpy(saved.original.data(), saved.storage, saved.bytes);
      saved.replacement = saved.original;
      saved.link = const_cast<Link *>(&pd149_test::incoming(*tree_, *saved.socket));
      require(saved.link->tonode == principled_,
              "Surface reference target link ownership differs");
      if (index == 0) {
        std::array<float, 3> rgb;
        for (int c = 0; c < 3; ++c) {
          require(std::isfinite(value_.color[c]) && std::isfinite(float(value_.color[c])),
                  "Surface reference nonfinite Color");
          rgb[c] = float(value_.color[c]);
        }
        std::memcpy(saved.replacement.data(), rgb.data(), sizeof(rgb));
      }
      else {
        const double scalar = index == 1 ? value_.metallic : value_.roughness;
        require(std::isfinite(scalar) && scalar >= 0 && scalar <= 1,
                "Surface reference scalar outside 0..1");
        const float replacement = float(scalar);
        std::memcpy(saved.replacement.data() + offsetof(bNodeSocketValueFloat, value),
                    &replacement,
                    sizeof(replacement));
      }
    }
    for (auto &link : tree_->links) {
      order_.push_back(&link);
      links_.push_back({&link,
                        link.fromnode,
                        link.tonode,
                        link.fromsock,
                        link.tosock,
                        int(link.flag),
                        link.multi_input_sort_id});
    }
    lease_.emplace(
        main_,
        material_,
        *principled_,
        std::array<Socket *, 3>{sockets_[0].socket, sockets_[1].socket, sockets_[2].socket});
    active_ = true;
    try {
      for (auto &saved : sockets_) {
        BLI_remlink(&tree_->links, saved.link);
        saved.detached = true;
        BKE_ntree_update_tag_link_removed(tree_);
        std::memcpy(saved.storage, saved.replacement.data(), saved.bytes);
        BKE_ntree_update_tag_socket_property(tree_, saved.socket);
      }
      updated();
      verify();
    }
    catch (...) {
      restore_owned();
      throw;
    }
  }
  const UniformReference &values() const
  {
    return value_;
  }
  void verify() const
  {
    owner();
    pd149_test::require(active_, "Surface reference is not active");
    pd149_test::require(graph() == expected(),
                        "Surface reference changed something outside three owned inputs");
    for (const auto &saved : sockets_)
      pd149_test::require(saved.detached && saved.socket->default_value == saved.storage &&
                              std::memcmp(saved.storage, saved.replacement.data(), saved.bytes) ==
                                  0,
                          "Surface reference input/default identity changed");
    for (const auto &saved : links_)
      pd149_test::require(saved.link->fromnode == saved.from && saved.link->tonode == saved.to &&
                              saved.link->fromsock == saved.out &&
                              saved.link->tosock == saved.in &&
                              int(saved.link->flag) == saved.flags &&
                              saved.link->multi_input_sort_id == saved.sort,
                          "Surface reference original link identity/settings changed");
  }
  void restore()
  {
    if (!active_)
      return;
    verify();
    restore_owned();
  }
  ~PrincipledReference()
  {
    if (active_)
      try {
        restore();
      }
      catch (...) {
        qFatal("Surface reference escaped exact owner restoration");
      }
  }
  PrincipledReference(const PrincipledReference &) = delete;
  PrincipledReference &operator=(const PrincipledReference &) = delete;
  PrincipledReference(PrincipledReference &&) = delete;
  PrincipledReference &operator=(PrincipledReference &&) = delete;
};
}  // namespace bts::pd149_surface_test
