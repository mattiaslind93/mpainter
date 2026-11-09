// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SpsmMeshMapLowering.hh"
#include <QCryptographicHash>
#include <QFile>
#include <iostream>
namespace bts::spsm_meshmap_test {
namespace s = spsm;
inline void require(bool yes, const char *why)
{
  if (!yes)
    throw std::runtime_error(why);
}
inline void near(float a, float b, const char *why)
{
  if (!std::isfinite(a) || std::abs(a - b) > 3e-6f) {
    std::cerr << why << " actual=" << a << " expected=" << b << '\n';
    throw std::runtime_error(why);
  }
}
inline bool has(const PreparedSpsm &p, std::string_view code)
{
  return std::any_of(
      p.diagnostics.begin(), p.diagnostics.end(), [&](const auto &d) { return d.code == code; });
}
inline const TextureLayer &leaf(const PreparedSpsm &p)
{
  for (const auto &l : p.material.set.layers)
    if (l.name == "Occlusion")
      return l;
  throw std::runtime_error("Missing Occlusion layer");
}
inline SpsmMeshMapOptions policy()
{
  return {SpsmMeshMapPolicy::NativeAOGeneratorUVIdentityNearest};
}
inline SpsmImportOptions color_policy()
{
  SpsmImportOptions o;
  o.uniform_rgb_to_scene_linear = [](auto rgb) { return rgb; };
  return o;
}
struct Builder {
  s::Preset p;
  s::NodeId src, bitmap, fill, leaf_id, root, uv0, uv1, layerblend, actionblend;
  template<class T> s::NodeId add(T v)
  {
    auto id = p.nodes.size();
    p.nodes.push_back({18, 0, 1, std::move(v)});
    return id;
  }
  s::NodeId integer(std::uint64_t x)
  {
    return add(x);
  }
  s::NodeId flag(bool x)
  {
    return add(x);
  }
  s::NodeId scalar(float x)
  {
    s::FloatValues v;
    v.count = 1;
    v.values[0] = x;
    return add(v);
  }
  s::NodeId pair(float x, float y)
  {
    s::FloatValues v;
    v.count = 2;
    v.values[0] = x;
    v.values[1] = y;
    return add(v);
  }
  s::NodeId rgb(float x, float y, float z)
  {
    s::FloatValues v;
    v.count = 3;
    v.values = {x, y, z};
    return add(v);
  }
  s::NodeId object(std::string name, std::initializer_list<s::Field> f)
  {
    return add(s::Object{std::move(name), f});
  }
  s::NodeId array(std::vector<s::NodeId> v)
  {
    return add(s::Array{std::move(v)});
  }
  s::NodeId stack(std::string name, std::vector<s::NodeId> v)
  {
    return object(std::move(name), {{"items", array(std::move(v))}});
  }
  void set(s::NodeId id, std::string_view name, s::NodeId v)
  {
    for (auto &f : std::get<s::Object>(p.nodes[id].value).fields)
      if (f.name == name) {
        f.value = v;
        return;
      }
    std::get<s::Object>(p.nodes[id].value).fields.push_back({std::string(name), v});
  }
  s::NodeId blend(unsigned mode, float opacity)
  {
    return object("DataBlending",
                  {{"channelTypes", integer(1)},
                   {"blendingMode", integer(mode)},
                   {"opacity", scalar(opacity)}});
  }
  s::NodeId uv()
  {
    return object("Data2DTransformation",
                  {{"GUIPivot", pair(.5, .5)},
                   {"GUIScaleRatioLocked", flag(true)},
                   {"rotation", scalar(0)},
                   {"scale", pair(1, 1)},
                   {"translation", pair(0, 0)}});
  }
  Builder()
  {
    bitmap = object("DataBitmap",
                    {{"alphaType", integer(0)},
                     {"colorSpace", integer(0)},
                     {"urlToBitmapRes", add(std::string("resourcealias:AmbientOcclusion"))}});
    src = object("DataSourceBitmap",
                 {{"bitmap", bitmap}, {"channelTypes", integer(1)}, {"opacity", scalar(.7f)}});
    uv0 = uv();
    uv1 = uv();
    layerblend = blend(4, .6f);
    actionblend = blend(2, .8f);
    fill = object("DataActionFill",
                  {{"enabled", flag(true)},
                   {"channelTypes", integer(UINT64_MAX)},
                   {"sources", array({src})},
                   {"perChannelBlending", array({actionblend})},
                   {"projection", integer(0)},
                   {"filtering", integer(2)},
                   {"uvSamplingWrap", integer(3)},
                   {"uvTransformation0", uv0},
                   {"uvTransformation1", uv1},
                   {"backfaceCulling", flag(false)},
                   {"depthCulling", flag(false)},
                   {"tags", integer(0)},
                   {"selection", add(s::Blob{})}});
    leaf_id = object("DataLayerColor",
                     {{"label", add(std::string("Occlusion"))},
                      {"enabled", flag(true)},
                      {"actions", stack("DataStackActions", {fill})},
                      {"maskEnabled", flag(true)},
                      {"maskInitial", scalar(.4f)},
                      {"perChannelBlending", array({layerblend})}});
    auto uniform = object(
        "DataSourceUniform",
        {{"channelTypes", integer(1)}, {"color", rgb(.8, .4, .2)}, {"opacity", scalar(1)}});
    auto basefill = object(
        "DataActionFill",
        {{"enabled", flag(true)}, {"channelTypes", integer(1)}, {"sources", array({uniform})}});
    auto base = object("DataLayerColor",
                       {{"label", add(std::string("Base"))},
                        {"enabled", flag(true)},
                        {"actions", stack("DataStackActions", {basefill})},
                        {"maskEnabled", flag(false)}});
    root = object("DataLayerGroup",
                  {{"label", add(std::string("root"))},
                   {"enabled", flag(true)},
                   {"maskEnabled", flag(true)},
                   {"maskInitial", scalar(1)},
                   {"subStack", stack("DataStackLayers", {base, leaf_id})}});
    p.root = object("DataLayersPreset", {{"layerGroup", root}});
    p.dataset = "/preset.bin";
    p.encoded = {1, 2, 3, 4};
  }
  PreparedSpsm prepare()
  {
    std::vector<s::Node> nodes;
    std::function<s::NodeId(s::NodeId)> clone = [&](s::NodeId id) {
      auto n = p.nodes.at(id);
      if (auto o = std::get_if<s::Object>(&n.value))
        for (auto &f : o->fields)
          f.value = clone(f.value);
      if (auto a = std::get_if<s::Array>(&n.value))
        for (auto &v : a->elements)
          v = clone(v);
      auto next = nodes.size();
      nodes.push_back(std::move(n));
      return next;
    };
    p.root = clone(p.root);
    p.nodes = std::move(nodes);
    s::Archive a;
    a.presets.push_back(std::move(p));
    return prepare_spsm_material(spsm::semantic::inspect_archive(std::move(a)), color_policy());
  }
};
// Genuine native premultiplied RGBA. All combinations include zero and partial
// coverage. R differs by tile and rebake revision; G/B intentionally differ.
inline float scalar(int tile, int x, int y, int revision = 0)
{
  return float((x * 3 + y * 7 + (tile - 1001) * 11 + revision * 13) % 33) / 32;
}
inline float coverage(int x, int y)
{
  return (x + y) % 5 == 0 ? 0 : (x + y) % 2 ? .25f : 1;
}
inline void maps(TextureSet &set, int revision = 0)
{
  MeshMap map;
  map.name = "Ambient occlusion";
  for (int tile : set.tiles) {
    std::vector<float> values(32 * 32 * 4);
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 32; ++x) {
        const auto i = (y * 32 + x) * 4;
        const auto a = coverage(x, y);
        values[i] = scalar(tile, x, y, revision) * a;
        values[i + 1] = .9f * a;
        values[i + 2] = .1f * a;
        values[i + 3] = a;
      }
    auto pixels = std::make_shared<MeshMapPixels>(32, std::move(values));
    map.tiles[tile] = {32, pixels->hash, pixels};
  }
  set.mesh_maps[3] = std::move(map);
}
inline TextureSet destination()
{
  TextureSet set;
  set.id = QUuid::createUuid();
  set.name = "AO alias fixture";
  set.tiles = {1001, 1002};
  set.layers.front().visible = false;
  set.layers.front().channels.fill(false);
  set.uniform_editable = true;
  set.texture_resolution = 128;
  set.bump_origin = .5f;
  set.precision[0] = ChannelPrecision::RGB32F;
  maps(set);
  return set;
}
inline std::array<float, 3> expected(
    int tile, int x, int y, int revision = 0, float opacity = .6f * .8f * .7f * .4f)
{
  const float value = coverage(x / 4, y / 4) > 1e-8 ? scalar(tile, x / 4, y / 4, revision) : 0;
  std::array<float, 3> result{.8f, .4f, .2f};
  for (auto &c : result)
    c = std::lerp(c, c * value, opacity);
  return result;
}
inline QByteArray file_hash(const QString &path)
{
  QFile f(path);
  require(f.open(QIODevice::ReadOnly), "Cannot open source for SHA");
  QCryptographicHash hash(QCryptographicHash::Sha256);
  require(hash.addData(&f), "Cannot checksum source");
  return hash.result().toHex();
}
}  // namespace bts::spsm_meshmap_test
