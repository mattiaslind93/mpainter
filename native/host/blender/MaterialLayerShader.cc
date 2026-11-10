// SPDX-License-Identifier: GPL-3.0-or-later
#include "MaterialLayerShader.hh"
#include "MaterialLayeringIO.hh"
#include "MaterialLayeringSourceSnapshots.hh"
#include "SceneSnapshot.hh"
#include "BKE_image.hh"
#include "BKE_idprop.hh"
#include "FillTextureJSON.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DEG_depsgraph.hh"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "IMB_colormanagement.hh"
#include "BLI_math_matrix.hh"
#include "opencolorio/OCIO_matrix.hh"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <cmath>
#include <string>
#include <set>
#include <vector>
#include "DNA_texture_types.h"
#include "MaterialLayerReferenceLease.hh"
namespace bts {
namespace {
using namespace blender;
struct Source {
  bNode *node = nullptr;
  const char *socket = "Color";
};
struct Builder {
  bNodeTree &tree;
  bool changed = false;
  bNode &node(const char *type, const std::string &name)
  {
    if (auto *n = bke::node_find_node_by_name(tree, name.c_str())) {
      if (!STREQ(n->idname, type))
        throw std::runtime_error("Material layering node type was changed");
      return *n;
    }
    auto *n = bke::node_add_node(nullptr, tree, UString(type));
    if (!n)
      throw std::runtime_error("Cannot create material layering node");
    STRNCPY(n->name, name.c_str());
    changed = true;
    return *n;
  }
  bNodeSocket &in(bNode &n, int i)
  {
    int at = 0;
    for (auto &socket : n.inputs)
      if (at++ == i)
        return socket;
    throw std::runtime_error("Material layering input missing");
  }
  bNodeSocket &in(bNode &n, const char *name)
  {
    auto *s = bke::node_find_enabled_input_socket(n, name);
    if (!s)
      throw std::runtime_error("Material layering named input missing");
    return *s;
  }
  bNodeSocket &out(Source s)
  {
    for (auto &socket : s.node->outputs)
      if (STREQ(socket.identifier, s.socket))
        return socket;
    throw std::runtime_error("Material layering output missing");
  }
  void connect(Source from, bNode &n, bNodeSocket &to)
  {
    if (STREQ(to.identifier, "Base Color"))
      for (const auto &link : tree.links)
        if (link.tosock == &to && STREQ(link.fromnode->name, "__BTS_OpenPbrBaseWeight")) {
          connect(from, *link.fromnode, in(*link.fromnode, "Color1"));
          return;
        }
    auto &socket = out(from);
    for (const auto &link : tree.links)
      if (link.fromsock == &socket && link.tosock == &to)
        return;
    bke::node_remove_socket_links(tree, to);
    bke::node_add_link(tree, *from.node, socket, n, to);
    changed = true;
  }
  void scalar(bNodeSocket &s, float v)
  {
    if (s.type != SOCK_FLOAT || !s.default_value)
      throw std::runtime_error("Material scalar socket mismatch");
    auto &old = static_cast<bNodeSocketValueFloat *>(s.default_value)->value;
    if (old != v) {
      old = v;
      BKE_ntree_update_tag_socket_property(&tree, &s);
      changed = true;
    }
  }
  void vector(bNodeSocket &s, std::array<float, 3> v)
  {
    if (s.type != SOCK_VECTOR || !s.default_value)
      throw std::runtime_error("Material vector socket mismatch");
    auto *old = static_cast<bNodeSocketValueVector *>(s.default_value)->value;
    if (!std::equal(v.begin(), v.end(), old)) {
      std::copy(v.begin(), v.end(), old);
      BKE_ntree_update_tag_socket_property(&tree, &s);
      changed = true;
    }
  }
  Source rgb(const std::string &name, std::array<float, 4> v)
  {
    auto &n = node("ShaderNodeRGB", name);
    auto &socket = out({&n});
    auto *old = static_cast<bNodeSocketValueRGBA *>(socket.default_value)->value;
    if (!std::equal(v.begin(), v.end(), old)) {
      std::copy(v.begin(), v.end(), old);
      BKE_ntree_update_tag_socket_property(&tree, &socket);
      changed = true;
    }
    return {&n};
  }
  Source value(const std::string &name, float v)
  {
    auto &n = node("ShaderNodeValue", name);
    scalar(out({&n, "Value"}), v);
    return {&n, "Value"};
  }
  void operation(bNode &n, int op)
  {
    if (n.custom1 != op) {
      n.custom1 = op;
      BKE_ntree_update_tag_node_property(&tree, &n);
      changed = true;
    }
  }
  Source vec(const std::string &name, int op, Source a, std::array<float, 3> v)
  {
    auto &n = node("ShaderNodeVectorMath", name);
    operation(n, op);
    connect(a, n, in(n, 0));
    vector(in(n, 1), v);
    return {&n, "Vector"};
  }
  Source mix(const std::string &name, Source factor, Source a, Source c)
  {
    auto &n = node("ShaderNodeMixRGB", name);
    operation(n, MA_RAMP_BLEND);
    connect(factor, n, in(n, 0));
    connect(a, n, in(n, 1));
    connect(c, n, in(n, 2));
    return {&n};
  }
  Source binary_vector(
      const std::string &name, int op, Source a, Source c, const char *output = "Vector")
  {
    auto &n = node("ShaderNodeVectorMath", name);
    operation(n, op);
    connect(a, n, in(n, 0));
    connect(c, n, in(n, 1));
    return {&n, output};
  }
  Source math(const std::string &name, int op, Source a, Source c)
  {
    auto &n = node("ShaderNodeMath", name);
    operation(n, op);
    connect(a, n, in(n, 0));
    connect(c, n, in(n, 1));
    return {&n, "Value"};
  }
  Source product(const std::string &name, Source a, Source c)
  {
    auto &n = node("ShaderNodeMixRGB", name);
    operation(n, MA_RAMP_MULT);
    scalar(in(n, 0), 1);
    connect(a, n, in(n, 1));
    connect(c, n, in(n, 2));
    return {&n};
  }
};
void release_image(Builder &b, const std::string &name)
{
  if (auto *n = bke::node_find_node_by_name(b.tree, name.c_str()); n && n->id) {
    id_us_min(n->id);
    n->id = nullptr;
    BKE_ntree_update_tag_node_property(&b.tree, n);
    b.changed = true;
  }
}
Source image_source(Builder &b, const std::string &name, Image &image, Source uv)
{
  auto &n = b.node("ShaderNodeTexImage", name);
  auto *settings = static_cast<NodeTexImage *>(n.storage);
  settings->extension = SHD_IMAGE_EXTENSION_REPEAT;
  settings->interpolation = SHD_INTERP_LINEAR;
  if (n.id != &image.id) {
    if (n.id)
      id_us_min(n.id);
    n.id = &image.id;
    id_us_plus(n.id);
    BKE_ntree_update_tag_node_property(&b.tree, &n);
    b.changed = true;
  }
  b.connect(uv, n, b.in(n, "Vector"));
  return {&n};
}
#include "MaterialLayerSrgbFilter.inc"
#include "MaterialLayerRawUdim.inc"
#include "MaterialLayerColorUdim.inc"
}  // namespace
std::map<int, bNode *> MaterialLayerShader::raw_udim_samples(
    Main &main, Material &material, int input, int channel, const FillTexture &source)
{
  if (channel != 1 && channel != 2 && channel != 4 && channel != 5)
    throw std::invalid_argument("Raw UDIM role is not scalar");
  Builder builder{*material.nodetree};
  const auto prefix = "__BTS_LayerMaterial" + std::to_string(input) + "ScalarMap" +
                      std::to_string(channel) + "Udim";
  RawUdimFilter filter{builder, prefix, source, true};
  filter.build();
  filter.audit();
  for (const auto &[tile, node] : filter.samples) {
    bool owned = false;
    for (Image &image : main.images)
      owned |= node->id == &image.id;
    if (!owned)
      throw std::invalid_argument("Raw UDIM Image is not Main-owned");
  }
  return filter.samples;
}
std::map<int, bNode *> MaterialLayerShader::color_udim_samples(Main &main,
                                                               Material &material,
                                                               int input,
                                                               const FillTexture &source)
{
  if (!material.nodetree || input < 0 || input >= 4)
    throw std::invalid_argument("Invalid linear Color UDIM binding");
  const auto root = "__BTS_LayerMaterial" + std::to_string(input), prefix = root + "ColorUdim";
  const auto provenance = color_udim_source(*material.nodetree, prefix);
  Builder builder{*material.nodetree};
  ColorUdimFilter filter{builder, prefix, root, source, provenance.encoded, true};
  filter.build();
  filter.audit();
  for (const auto &[tile, node] : filter.samples) {
    bool owned = false;
    for (Image &image : main.images)
      owned |= node->id == &image.id;
    if (!owned)
      throw std::invalid_argument("Linear Color UDIM Image is not Main-owned");
  }
  return filter.samples;
}
struct MaterialLayerShader::Impl {
  bool installed = false;
  std::string key;
  std::array<std::shared_ptr<const FillTexture>, 4> colors{};
  std::array<std::uint32_t, 4> color_ids{};
};
MaterialLayerShader::MaterialLayerShader() : impl_(std::make_unique<Impl>()) {}
MaterialLayerShader::~MaterialLayerShader() = default;
MaterialLayerShader::MaterialLayerShader(MaterialLayerShader &&) noexcept = default;
MaterialLayerShader &MaterialLayerShader::operator=(MaterialLayerShader &&) noexcept = default;
bool MaterialLayerShader::apply(Main &main,
                                Material &material,
                                const MaterialLayering &model,
                                float normal_intensity,
                                float ao_intensity,
                                bool directx,
                                const MaterialLayerImages &images)
{
  // Complete source preparation is a precondition, not a lazy decode path.
  if (model.enabled) {
    model.validate();
    for (const auto &input : model.materials)
      if (input.textures[0] && input.textures[0]->tiles)
        material_layer_color_source_transforms(*input.textures[0]);
  }
  if (model.enabled)
    for (int i = 0; i < 4; ++i)
      for (int c = 0; c < 6; ++c)
        if (model.materials[i].textures[c] && !images[i][c] && images.members[i][c].empty())
          throw std::runtime_error("Reusable material source is not GPU-ready");
  const bool color_map = model.enabled &&
                         std::any_of(model.materials.begin(),
                                     model.materials.end(),
                                     [](const auto &input) { return bool(input.textures[0]); });
  bool invertible = false;
  float3x3 from_aces = float3x3::identity();
  if (color_map) {
    from_aces = math::invert(main.colorspace.scene_linear_to_xyz, invertible) *
                ocio::ACESCG_TO_XYZ;
    if (!invertible)
      throw std::invalid_argument("Material working-space matrix is singular");
    for (int column = 0; column < 3; ++column)
      for (int row = 0; row < 3; ++row)
        if (!std::isfinite(from_aces[column][row]))
          throw std::invalid_argument("Material working-space matrix is nonfinite");
  }
  auto &state = *impl_;
  if (!model.enabled && !state.installed &&
      (!material.nodetree ||
       !bke::node_find_node_by_name(*material.nodetree, "__BTS_LayeringEnabled")))
    return false;
  auto *principled = native_principled_surface(material);
  if (!principled)
    throw std::runtime_error(
        "Material layering requires the original supported Principled surface");
  Builder b{*material.nodetree};
  const auto retire_srgb = [&](int input, bool preserve_runtime = false) {
    const auto prefix = "__BTS_LayerMaterial" + std::to_string(input);
    auto *marker = bke::node_find_node_by_name(b.tree, prefix + "SrgbMixed");
    if (!marker) {
      for (const auto &node : b.tree.nodes) {
        const std::string name = node.name;
        if (name.starts_with(prefix + "Srgb") || name.starts_with(prefix + "ColorTap"))
          throw std::invalid_argument(
              "Incomplete or foreign sRGB namespace cannot be overwritten");
      }
      return;
    }
    bool encoded = false;
    const auto recorded = srgb_filter_source(b.tree, prefix, encoded);
    const auto old = state.colors[input] ? state.colors[input] : recorded;
    if (!old || !material_layer_source_color_transform(*old).srgb)
      throw std::invalid_argument("Unowned sRGB filter cannot be discarded");
    // A fresh inspection can replace an earlier runtime graph or a stock graph.
    // Read its typed value provenance, then verify the ENTIRE graph; a name or
    // representation marker alone never authorizes deletion.
    auto *sample = bke::node_find_node_by_name(b.tree, prefix + "Color");
    bool owned = false;
    for (const Image &image : main.images)
      if (sample && sample->id == &image.id &&
          (!state.colors[input] || image.id.session_uid == state.color_ids[input]))
        owned = true;
    if (!owned)
      throw std::invalid_argument("sRGB filter Image identity changed");
    color_source_representation(main, material, input, *old, encoded, true);
    if (preserve_runtime && state.colors[input] && !encoded) {
      const auto &next = model.materials[input].textures[0];
      if (next && !next->tiles && material_layer_source_color_transform(*next).srgb)
        return;
    }
    SrgbFilter filter{b, prefix, *old, encoded, true};
    filter.build();
    filter.audit();
    filter.remove(main);
  };
  const auto retire_udim = [&](int input, int channel) {
    const auto prefix = "__BTS_LayerMaterial" + std::to_string(input) + "ScalarMap" +
                        std::to_string(channel) + "Udim";
    if (!bke::node_find_node_by_name(b.tree, prefix + "Representation")) {
      for (const auto &n : b.tree.nodes)
        if (std::string(n.name).starts_with(prefix))
          throw std::invalid_argument("Unowned raw UDIM namespace cannot be overwritten");
      return;
    }
    const auto source = udim_filter_source(b.tree, prefix);
    RawUdimFilter filter{b, prefix, *source, true};
    filter.build();
    filter.audit();
    for (const auto &[tile, n] : filter.samples) {
      bool owned = false;
      for (Image &image : main.images)
        owned |= n->id == &image.id;
      if (!owned)
        throw std::invalid_argument("Raw UDIM sample Image is outside its Main");
    }
    filter.remove(main);
  };
  const auto retire_color_udim = [&](int input) {
    const auto root = "__BTS_LayerMaterial" + std::to_string(input), prefix = root + "ColorUdim";
    if (!bke::node_find_node_by_name(b.tree, prefix + "Representation")) {
      for (const auto &n : b.tree.nodes)
        if (std::string(n.name).starts_with(prefix))
          throw std::invalid_argument("Unowned linear Color UDIM namespace cannot be overwritten");
      return;
    }
    const auto old = color_udim_source(b.tree, prefix);
    color_source_representation(main, material, input, *old.source, old.encoded, true);
    ColorUdimFilter filter{b, prefix, root, *old.source, old.encoded, true};
    filter.build();
    filter.audit();
    for (const auto &[tile, n] : filter.samples) {
      bool owned = false;
      for (Image &image : main.images)
        owned |= n->id == &image.id;
      if (!owned)
        throw std::invalid_argument("Linear Color UDIM Image is outside its Main");
    }
    filter.remove(main);
  };
  const auto attach = [&](const char *name, Source source) {
    b.connect(source, *principled, b.in(*principled, name));
  };
  b.value("__BTS_LayeringEnabled", model.enabled ? 1.f : 0.f);
  b.value("__BTS_LayerUvIsData", 0.f);  // View selection belongs to the private UV material.
  if (!model.enabled) {
    for (int i = 0; i < 4; ++i) {
      retire_srgb(i);
      retire_color_udim(i);
      for (int c : {1, 2, 4, 5})
        retire_udim(i, c);
    }
    state.colors = {};
    const char *dest[]{"Base Color", "Metallic", "Roughness"};
    for (int c = 0; c < 3; ++c) {
      auto *n = bke::node_find_node_by_name(
          b.tree, c == 0 ? "__BTS_AOMaterial" : ("__BTS_BaseMix" + std::to_string(c)).c_str());
      if (!n && c == 0)
        n = bke::node_find_node_by_name(b.tree, "__BTS_BaseMix0");
      if (n)
        attach(dest[c], {n});
    }
    auto *world = bke::node_find_node_by_name(b.tree, "__BTS_NativeWorldFinal");
    auto *bump = bke::node_find_node_by_name(b.tree, "__BTS_PaintHeight");
    if (world)
      attach("Normal", {world, "Vector"});
    else if (bump)
      attach("Normal", {bump, "Normal"});
    for (int i = 0; i < 4; ++i) {
      const auto prefix = "__BTS_LayerMaterial" + std::to_string(i);
      for (const char *suffix :
           {"Color", "Controls", "Normal", "ScalarMap1", "ScalarMap2", "ScalarMap4", "ScalarMap5"})
        release_image(b, prefix + suffix);
    }
    state.installed = false;
    state.key.clear();
  }
  else {
    model.validate();
    QJsonArray image_ids;
    for (const auto &input : images)
      for (const auto *image : input)
        image_ids.append(image ? double(image->id.session_uid) : 0);
    for (const auto &input : images.members)
      for (const auto &role : input)
        for (const auto &[tile, image] : role)
          image_ids.append(QJsonArray{tile, double(image->id.session_uid)});
    QJsonArray working_matrix;
    for (int column = 0; column < 3; ++column)
      for (int row = 0; row < 3; ++row)
        working_matrix.append(from_aces[column][row]);
    const auto key = QJsonDocument(QJsonObject{{"working_matrix", working_matrix},
                                               {"source_images", image_ids},
                                               {"materials",
                                                [&] {
                                                  QJsonArray a;
                                                  for (const auto &v : model.materials)
                                                    a.append(layer_material_json(v));
                                                  return a;
                                                }()},
                                               {"normal_intensity", normal_intensity},
                                               {"ao_intensity", ao_intensity},
                                               {"directx", directx}})
                         .toJson(QJsonDocument::Compact)
                         .toStdString();
    std::array<Source, 3> masks;
    for (int i = 0; i < 3; ++i) {
      auto *image = bke::node_find_node_by_name(
          b.tree, ("__BTS_UserPixels" + std::to_string(model.mask_slots[i])).c_str());
      if (!image || !image->id)
        throw std::runtime_error("Shader opacity image is not allocated");
      auto &clamp = b.node("ShaderNodeClamp", "__BTS_LayerMask" + std::to_string(i));
      b.connect({image}, clamp, b.in(clamp, "Value"));
      b.scalar(b.in(clamp, "Min"), 0);
      b.scalar(b.in(clamp, "Max"), 1);
      masks[i] = {&clamp, "Result"};
    }
    if (state.key != key || !state.installed) {
      for (int i = 0; i < 4; ++i) {
        retire_srgb(i, true);
        retire_color_udim(i);
        for (int c : {1, 2, 4, 5})
          retire_udim(i, c);
      }
      std::array<std::array<Source, 6>, 4> sources;
      auto &coords = b.node("ShaderNodeTexCoord", "__BTS_LayerUvCoordinates");
      for (int i = 0; i < 4; ++i) {
        const auto &input = model.materials[i];
        const auto prefix = "__BTS_LayerMaterial" + std::to_string(i);
        auto &mapping = b.node("ShaderNodeMapping", prefix + "Mapping");
        b.operation(mapping, NODE_MAPPING_TYPE_POINT);
        b.connect({&coords, "UV"}, mapping, b.in(mapping, "Vector"));
        b.vector(b.in(mapping, "Location"), {float(input.offset[0]), float(input.offset[1]), 0});
        b.vector(b.in(mapping, "Rotation"),
                 {0, 0, float(input.rotation * 3.14159265358979323846 / 180)});
        b.vector(b.in(mapping, "Scale"), {float(input.tiling[0]), float(input.tiling[1]), 1});
        const Source uv{&mapping, "Vector"};
        if (auto *image = images[i][0]; image || !images.members[i][0].empty()) {
          Source color;
          if (!images.members[i][0].empty()) {
            release_image(b, prefix + "Color");
            color =
                ColorUdimFilter{b, prefix + "ColorUdim", prefix, *input.textures[0], false, false}
                    .build(&images.members[i][0]);
          }
          else {
            const auto transform = material_layer_source_color_transform(*input.textures[0]);
            color = transform.srgb ?
                        SrgbFilter{b, prefix, *input.textures[0], false, false}.build(image) :
                        image_source(b, prefix + "Color", *image, uv);
          }
          auto &combine = b.node("ShaderNodeCombineXYZ", prefix + "ColorToScene");
          const char *components[]{"X", "Y", "Z"};
          for (int row = 0; row < 3; ++row) {
            auto &dot = b.node("ShaderNodeVectorMath",
                               prefix + "ColorToScene" + std::to_string(row));
            b.operation(dot, NODE_VECTOR_MATH_DOT_PRODUCT);
            b.connect(color, dot, b.in(dot, 0));
            b.vector(b.in(dot, 1), {from_aces[0][row], from_aces[1][row], from_aces[2][row]});
            b.connect({&dot, "Value"}, combine, b.in(combine, components[row]));
          }
          sources[i][0] = {&combine, "Vector"};
        }
        else {
          release_image(b, prefix + "Color");
          sources[i][0] = b.rgb(prefix + "ColorValue", input.values.base_color);
        }
        // Each scalar samples its own original dimensions. R is authoritative;
        // alpha is never a control channel and signed bump remains unclamped.
        release_image(b, prefix + "Controls");
        const int scalar_channels[]{1, 2, 4, 5};
        const float values[]{input.values.metallic,
                             input.values.roughness,
                             input.values.bump,
                             input.values.ambient_occlusion};
        for (int n = 0; n < 4; ++n) {
          const int c = scalar_channels[n];
          const auto name = prefix + "ScalarMap" + std::to_string(c);
          if (auto *image = images[i][c]; image || !images.members[i][c].empty()) {
            const bool udim = !images.members[i][c].empty();
            if (udim)
              release_image(b, name);
            const auto sampled = udim ? RawUdimFilter{b, name + "Udim", *input.textures[c], false}
                                            .build(&images.members[i][c]) :
                                        image_source(b, name, *image, uv);
            auto &split = b.node("ShaderNodeSeparateXYZ", name + "Split");
            b.connect(sampled, split, b.in(split, "Vector"));
            sources[i][c] = {&split, "X"};
            if (c != 4) {
              auto &clamp = b.node("ShaderNodeClamp", name + "Clamp");
              b.connect(sources[i][c], clamp, b.in(clamp, "Value"));
              b.scalar(b.in(clamp, "Min"), 0);
              b.scalar(b.in(clamp, "Max"), 1);
              sources[i][c] = {&clamp, "Result"};
            }
          }
          else {
            release_image(b, name);
            sources[i][c] = b.value(prefix + "Scalar" + std::to_string(c), values[n]);
          }
        }
        Source encoded;
        if (auto *image = images[i][3]) {
          encoded = image_source(b, prefix + "Normal", *image, uv);
          if (directx) {
            encoded = b.vec(
                prefix + "NormalDirectXScale", NODE_VECTOR_MATH_MULTIPLY, encoded, {1, -1, 1});
            encoded = b.vec(
                prefix + "NormalDirectXOffset", NODE_VECTOR_MATH_ADD, encoded, {0, 1, 0});
          }
        }
        else {
          release_image(b, prefix + "Normal");
          encoded = b.rgb(prefix + "NormalValue",
                          {input.values.normal[0] * .5f + .5f,
                           input.values.normal[1] * .5f + .5f,
                           input.values.normal[2] * .5f + .5f,
                           1});
        }
        auto decoded = b.vec(
            prefix + "NormalDecodeScale", NODE_VECTOR_MATH_MULTIPLY, encoded, {2, 2, 2});
        decoded = b.vec(
            prefix + "NormalDecodeOffset", NODE_VECTOR_MATH_ADD, decoded, {-1, -1, -1});
        auto &rotate = b.node("ShaderNodeVectorRotate", prefix + "NormalRotate");
        b.operation(rotate, NODE_VECTOR_ROTATE_TYPE_AXIS);
        b.connect(decoded, rotate, b.in(rotate, "Vector"));
        b.vector(b.in(rotate, "Axis"), {0, 0, 1});
        b.scalar(b.in(rotate, "Angle"), float(-input.rotation * 3.14159265358979323846 / 180));
        auto oriented = b.vec(
            prefix + "NormalMirror",
            NODE_VECTOR_MATH_MULTIPLY,
            {&rotate, "Vector"},
            {input.tiling[0] < 0 ? -1.f : 1.f, input.tiling[1] < 0 ? -1.f : 1.f, 1});
        oriented = b.vec(
            prefix + "NormalEncodeScale", NODE_VECTOR_MATH_MULTIPLY, oriented, {.5, .5, .5});
        sources[i][3] = b.vec(
            prefix + "NormalEncodeOffset", NODE_VECTOR_MATH_ADD, oriented, {.5, .5, .5});
      }
      for (int c = 0; c < 6; ++c) {
        Source result = sources[0][c];
        for (int i = 0; i < 3; ++i)
          result = b.mix("__BTS_LayerMix" + std::to_string(c) + "_" + std::to_string(i),
                         masks[i],
                         result,
                         sources[i + 1][c]);
        auto &final = b.node("ShaderNodeMixRGB", "__BTS_LayeredChannel" + std::to_string(c));
        b.operation(final, MA_RAMP_BLEND);
        b.scalar(b.in(final, 0), 0);
        b.connect(result, final, b.in(final, 1));
      }
      auto *normal_channel = bke::node_find_node_by_name(b.tree, "__BTS_LayeredChannel3");
      auto detail = b.vec(
          "__BTS_LayerDetailScale", NODE_VECTOR_MATH_MULTIPLY, {normal_channel}, {2, 2, 2});
      detail = b.vec("__BTS_LayerDetailDecode", NODE_VECTOR_MATH_ADD, detail, {-1, -1, -1});
      Source geometry = b.rgb("__BTS_LayerGeometryFlat", {.5, .5, 1, 1});
      if (auto *image = bke::node_find_node_by_name(b.tree, "__BTS_AuthoredNormalPixels"))
        geometry = {image};
      auto base = b.vec(
          "__BTS_LayerGeometryScale", NODE_VECTOR_MATH_MULTIPLY, geometry, {2, 2, 2});
      base = b.vec("__BTS_LayerGeometryDecode", NODE_VECTOR_MATH_ADD, base, {-1, -1, -1});
      const auto t = b.vec("__BTS_LayerNormalT", NODE_VECTOR_MATH_ADD, base, {0, 0, 1}),
                 u = b.vec("__BTS_LayerNormalU", NODE_VECTOR_MATH_MULTIPLY, detail, {-1, -1, 1});
      const auto dot = b.binary_vector(
          "__BTS_LayerNormalDot", NODE_VECTOR_MATH_DOT_PRODUCT, t, u, "Value");
      auto &components = b.node("ShaderNodeSeparateXYZ", "__BTS_LayerNormalTComponents");
      b.connect(t, components, b.in(components, "Vector"));
      const auto denominator = b.math("__BTS_LayerNormalDenominator",
                                      NODE_MATH_MAXIMUM,
                                      {&components, "Z"},
                                      b.value("__BTS_LayerNormalEpsilon", 1e-6f));
      const auto ratio = b.math("__BTS_LayerNormalRatio", NODE_MATH_DIVIDE, dot, denominator);
      auto combined = b.binary_vector(
          "__BTS_LayerNormalScaled", NODE_VECTOR_MATH_MULTIPLY, t, ratio);
      combined = b.binary_vector(
          "__BTS_LayerNormalCombined", NODE_VECTOR_MATH_SUBTRACT, combined, u);
      combined = b.vec(
          "__BTS_LayerNormalEncodedScale", NODE_VECTOR_MATH_MULTIPLY, combined, {.5, .5, .5});
      combined = b.vec(
          "__BTS_LayerNormalEncodedOffset", NODE_VECTOR_MATH_ADD, combined, {.5, .5, .5});
      auto &normal = b.node("ShaderNodeNormalMap", "__BTS_LayeredNormal");
      b.connect(combined, normal, b.in(normal, "Color"));
      b.scalar(b.in(normal, "Strength"), normal_intensity);
      auto &bump = b.node("ShaderNodeBump", "__BTS_LayeredBump");
      auto *height = bke::node_find_node_by_name(b.tree, "__BTS_LayeredChannel4");
      Source final_height{height};
      if (auto *shared = bke::node_find_node_by_name(b.tree, "__BTS_BaseMix4"))
        final_height = b.math("__BTS_LayerSharedHeight", NODE_MATH_ADD, final_height, {shared});
      b.connect(final_height, bump, b.in(bump, "Height"));
      b.connect({&normal, "Normal"}, bump, b.in(bump, "Normal"));
      state.key = key;
      for (int i = 0; i < 4; ++i) {
        state.colors[i] = model.materials[i].textures[0];
        state.color_ids[i] = images[i][0] ? images[i][0]->id.session_uid : 0;
      }
    }
    auto *color = bke::node_find_node_by_name(b.tree, "__BTS_LayeredChannel0"),
         *ao = bke::node_find_node_by_name(b.tree, "__BTS_LayeredChannel5");
    Source geometry_ao = b.rgb("__BTS_LayerGeometryAOWhite", {1, 1, 1, 1});
    if (auto *image = bke::node_find_node_by_name(b.tree, "__BTS_AOPixels"))
      geometry_ao = {image};
    const auto full_ao = b.product("__BTS_LayeredCompleteAO", {ao}, geometry_ao);
    const auto weighted_ao = b.mix("__BTS_LayeredAOIntensity",
                                   b.value("__BTS_LayeredAOAmount", ao_intensity),
                                   b.rgb("__BTS_LayeredWhite", {1, 1, 1, 1}),
                                   full_ao);
    b.product("__BTS_LayeredColor", {color}, weighted_ao);
    const char *dest[]{"Base Color", "Metallic", "Roughness"};
    for (int c = 0; c < 3; ++c) {
      auto *n = bke::node_find_node_by_name(
          b.tree,
          c == 0 ? "__BTS_LayeredColor" : ("__BTS_LayeredChannel" + std::to_string(c)).c_str());
      if (!MaterialLayerReferenceLease::owns(main, material, *principled, dest[c]))
        attach(dest[c], {n});
    }
    auto *bump = bke::node_find_node_by_name(b.tree, "__BTS_LayeredBump");
    attach("Normal", {bump, "Normal"});
    auto &uv = b.node("ShaderNodeMixRGB", "__BTS_LayeredUvColor");
    b.operation(uv, MA_RAMP_BLEND);
    b.scalar(b.in(uv, 0), 0);
    b.connect({bke::node_find_node_by_name(b.tree, "__BTS_LayeredChannel0")}, uv, b.in(uv, 1));
    state.installed = true;
  }
  if (b.changed) {
    BKE_ntree_update_after_single_tree_change(main, b.tree);
    DEG_id_tag_update_ex(&main, &material.id, ID_RECALC_SHADING);
    DEG_id_tag_update_ex(&main, &b.tree.id, ID_RECALC_NTREE_OUTPUT);
  }
  return b.changed;
}
void MaterialLayerShader::color_source_representation(Main &main,
                                                      Material &material,
                                                      int index,
                                                      const FillTexture &source,
                                                      bool encoded,
                                                      bool validate_only)
{
  if (index < 0 || index >= 4 || !material.nodetree)
    throw std::invalid_argument("Invalid reusable color binding");
  bool invertible = false;
  float3x3 matrix = math::invert(main.colorspace.scene_linear_to_xyz, invertible) *
                    ocio::ACESCG_TO_XYZ;
  if (!invertible)
    throw std::invalid_argument("Material working-space matrix is singular");
  MaterialLayerSourceColorTransform transform;
  if (!source.tiles)
    transform = material_layer_source_color_transform(source);
  else
    material_layer_color_source_transforms(source);
  if (encoded && !source.tiles) {
    float3x3 input;
    for (int c = 0; c < 3; ++c)
      for (int r = 0; r < 3; ++r)
        input[c][r] = transform.to_aces[c * 3 + r];
    matrix = matrix * input;
  }
  auto &tree = *material.nodetree;
  const auto prefix = "__BTS_LayerMaterial" + std::to_string(index);
  const auto get = [&](const std::string &name, const char *type) -> bNode & {
    auto *node = bke::node_find_node_by_name(tree, name.c_str());
    if (!node || !STREQ(node->idname, type) || (node->flag & NODE_MUTED))
      throw std::invalid_argument("Reusable source color adapter node changed");
    return *node;
  };
  bNode *sample = nullptr;
  Source sampled;
  Builder filter_builder{tree};
  if (source.tiles) {
    const auto name = prefix + "ColorUdim";
    const auto old = color_udim_source(tree, name);
    ColorUdimFilter previous{filter_builder, name, prefix, source, old.encoded, true};
    sampled = previous.build();
    previous.audit();
    if (validate_only) {
      if (old.encoded != encoded)
        throw std::invalid_argument("Linear Color UDIM representation changed");
    }
    else
      sampled = ColorUdimFilter{filter_builder, name, prefix, source, encoded, false}.build();
  }
  else {
    sample = &get(prefix + "Color", "ShaderNodeTexImage");
    sampled = {sample};
  }
  if (transform.srgb) {
    if (validate_only) {
      SrgbFilter filter{filter_builder, prefix, source, encoded, true};
      sampled = filter.build();
      filter.audit();
    }
    else {
      // Convert only a graph already proven canonical in its previous form.
      SrgbFilter previous{filter_builder, prefix, source, !encoded, true};
      previous.build();
      previous.audit();
      auto *image = reinterpret_cast<Image *>(sample->id);
      previous.remove_curves(main);
      sampled = SrgbFilter{filter_builder, prefix, source, encoded, false}.build(image);
    }
  }
  auto &combine = get(prefix + "ColorToScene", "ShaderNodeCombineXYZ");
  Builder builder{tree};
  const char *components[]{"X", "Y", "Z"};
  const auto edge = [&](bNode &from, const char *identifier, bNode &to, bNodeSocket &socket) {
    int count = 0;
    for (const auto &link : tree.links)
      if (link.tosock == &socket) {
        if (link.fromnode != &from || !STREQ(link.fromsock->identifier, identifier) ||
            link.fromsock->type != (STREQ(identifier, "Color")  ? SOCK_RGBA :
                                    STREQ(identifier, "Vector") ? SOCK_VECTOR :
                                                                  SOCK_FLOAT) ||
            (link.flag & NODE_LINK_MUTED) || !(link.flag & NODE_LINK_VALID))
          throw std::invalid_argument("Reusable source color adapter edge changed");
        ++count;
      }
    if (count != 1)
      throw std::invalid_argument("Reusable source color adapter edge is missing");
  };
  for (int row = 0; row < 3; ++row) {
    auto &dot = get(prefix + "ColorToScene" + std::to_string(row), "ShaderNodeVectorMath");
    if (dot.custom1 != NODE_VECTOR_MATH_DOT_PRODUCT)
      throw std::invalid_argument("Reusable source color adapter operation changed");
    if (validate_only)
      edge(*sampled.node, sampled.socket, dot, builder.in(dot, 0));
    else
      builder.connect(sampled, dot, builder.in(dot, 0));
    edge(dot, "Value", combine, builder.in(combine, components[row]));
    auto &coefficient = builder.in(dot, 1);
    if (coefficient.type != SOCK_VECTOR || !coefficient.default_value)
      throw std::invalid_argument("Reusable source color adapter coefficient changed");
    for (const auto &link : tree.links)
      if (link.tosock == &coefficient)
        throw std::invalid_argument("Reusable source color matrix cannot be linked");
    const std::array<float, 3> expected{matrix[0][row], matrix[1][row], matrix[2][row]};
    for (float value : expected)
      if (!std::isfinite(value))
        throw std::invalid_argument("Reusable source color matrix is nonfinite");
    if (validate_only) {
      const auto *value =
          static_cast<const bNodeSocketValueVector *>(coefficient.default_value)->value;
      if (!std::equal(expected.begin(), expected.end(), value))
        throw std::invalid_argument(
            "Reusable source color matrix does not match its representation");
    }
    else
      builder.vector(coefficient, expected);
  }
  if (validate_only && !transform.srgb && !source.tiles) {
    int outgoing = 0;
    for (const auto &link : tree.links)
      if (link.fromnode == sample) {
        bool known = false;
        for (int r = 0; r < 3; ++r) {
          auto &dot = get(prefix + "ColorToScene" + std::to_string(r), "ShaderNodeVectorMath");
          known |= link.tonode == &dot && link.tosock == &builder.in(dot, 0) &&
                   STREQ(link.fromsock->identifier, "Color");
        }
        if (!known)
          throw std::invalid_argument("Reusable color sample has an unowned use");
        ++outgoing;
      }
    if (outgoing != 3)
      throw std::invalid_argument("Reusable color sample fanout changed");
  }
  if (builder.changed || filter_builder.changed)
    BKE_ntree_update_after_single_tree_change(main, tree);
}
void MaterialLayerShader::pack_images(Main &main)
{
  for (Image &image : main.images)
    if ((STRPREFIX(image.id.name + 2, "BTS Layer Input ") ||
         STRPREFIX(image.id.name + 2, "BTS Layer Source ")) &&
        image.id.us > 0 && !BKE_image_memorypack(&image))
      throw std::runtime_error("Cannot pack generic material image");
}
}  // namespace bts
