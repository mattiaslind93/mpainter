// SPDX-License-Identifier: GPL-3.0-or-later
// Standard-node raw UDIM fanout. Reuses per-file immutable Image aliases.
std::shared_ptr<const FillTexture> udim_filter_source(bNodeTree &tree, const std::string &prefix)
{
  auto *n = bke::node_find_node_by_name(tree, prefix + "Representation");
  if (!n || !STREQ(n->idname, "ShaderNodeValue") || n->id || (n->flag & NODE_MUTED) || !n->prop ||
      n->prop->type != IDP_GROUP || BLI_listbase_count(&n->prop->data.group) != 1)
    throw std::invalid_argument("Raw UDIM provenance is missing");
  auto *p = IDP_GetPropertyFromGroup(n->prop, "source");
  if (!p || p->type != IDP_STRING || p->len < 1 || !IDP_string_get(p) ||
      IDP_string_get(p)[p->len - 1])
    throw std::invalid_argument("Raw UDIM provenance changed");
  QJsonParseError error;
  const auto json = QJsonDocument::fromJson(QByteArray::fromRawData(IDP_string_get(p), p->len - 1),
                                            &error);
  if (error.error != QJsonParseError::NoError || !json.isObject())
    throw std::invalid_argument("Invalid raw UDIM metadata");
  auto source = read_fill_texture_json(json.object());
  if (!source->raw || !source->tiles || fill_texture_json(source).toObject() != json.object())
    throw std::invalid_argument("Unsupported raw UDIM graph provenance");
  return source;
}
struct RawUdimFilter {
  Builder &b;
  std::string prefix;
  const FillTexture &source;
  bool check;
  std::set<bNode *> nodes;
  std::set<std::pair<bNodeSocket *, bNodeSocket *>> edges;
  std::map<int, bNode *> samples;
  bNode &node(const char *type, const std::string &suffix)
  {
    const auto name = prefix + suffix;
    auto *n = bke::node_find_node_by_name(b.tree, name);
    if (check) {
      if (!n || !STREQ(n->idname, type) || (n->flag & NODE_MUTED) ||
          (n->prop && suffix != "Representation") || (!STREQ(type, "ShaderNodeTexImage") && n->id))
        throw std::invalid_argument("Raw UDIM filter node provenance changed");
    }
    else
      n = &b.node(type, name);
    nodes.insert(n);
    return *n;
  }
  void op(bNode &n, int operation)
  {
    if (check) {
      if (n.custom1 != operation || n.custom2)
        throw std::invalid_argument("Raw UDIM filter operation changed");
    }
    else {
      b.operation(n, operation);
      if (n.custom2) {
        n.custom2 = 0;
        BKE_ntree_update_tag_node_property(&b.tree, &n);
        b.changed = true;
      }
    }
  }
  void scalar(bNodeSocket &s, float value)
  {
    if (check) {
      if (s.type != SOCK_FLOAT || !s.default_value ||
          static_cast<const bNodeSocketValueFloat *>(s.default_value)->value != value)
        throw std::invalid_argument("Raw UDIM filter scalar changed");
      for (const auto &l : b.tree.links)
        if (l.tosock == &s)
          throw std::invalid_argument("Raw UDIM filter constant is linked");
    }
    else
      b.scalar(s, value);
  }
  void vector(bNodeSocket &s, std::array<float, 3> value)
  {
    if (check) {
      if (s.type != SOCK_VECTOR || !s.default_value ||
          !std::equal(value.begin(),
                      value.end(),
                      static_cast<const bNodeSocketValueVector *>(s.default_value)->value))
        throw std::invalid_argument("Raw UDIM filter vector changed");
      for (const auto &l : b.tree.links)
        if (l.tosock == &s)
          throw std::invalid_argument("Raw UDIM filter constant is linked");
    }
    else
      b.vector(s, value);
  }
  void edge(Source from, bNode &to, bNodeSocket &socket)
  {
    auto &output = b.out(from);
    const int output_type = STREQ(from.socket, "Color")  ? SOCK_RGBA :
                            STREQ(from.socket, "Vector") ? SOCK_VECTOR :
                                                           SOCK_FLOAT;
    const int input_type = STREQ(to.idname, "ShaderNodeMixRGB") ?
                               (&socket == to.inputs.first ? SOCK_FLOAT : SOCK_RGBA) :
                           (STREQ(to.idname, "ShaderNodeMath") ||
                            STREQ(to.idname, "ShaderNodeCombineXYZ")) ?
                               SOCK_FLOAT :
                               SOCK_VECTOR;
    if (output.type != output_type || socket.type != input_type || (output.flag & SOCK_UNAVAIL) ||
        (socket.flag & SOCK_UNAVAIL))
      throw std::invalid_argument("Raw UDIM filter socket type or availability changed");
    edges.emplace(&output, &socket);
    if (check) {
      int count = 0;
      for (const auto &l : b.tree.links)
        if (l.tosock == &socket) {
          if (l.fromnode != from.node || l.fromsock != &output || l.tonode != &to ||
              !(l.flag & NODE_LINK_VALID) || (l.flag & NODE_LINK_MUTED))
            throw std::invalid_argument("Raw UDIM filter edge changed");
          ++count;
        }
      if (count != 1)
        throw std::invalid_argument("Raw UDIM filter edge is missing");
    }
    else
      b.connect(from, to, socket);
  }
  Source constant(const std::string &name, float value)
  {
    auto &n = node("ShaderNodeValue", name);
    scalar(b.out({&n, "Value"}), value);
    return {&n, "Value"};
  }
  Source math(const std::string &name, int operation, Source a, Source c)
  {
    auto &n = node("ShaderNodeMath", name);
    op(n, operation);
    edge(a, n, b.in(n, 0));
    edge(c, n, b.in(n, 1));
    return {&n, "Value"};
  }
  Source vector(const std::string &name, int operation, Source a, std::array<float, 3> value)
  {
    auto &n = node("ShaderNodeVectorMath", name);
    op(n, operation);
    edge(a, n, b.in(n, 0));
    vector(b.in(n, 1), value);
    return {&n, "Vector"};
  }
  Source mix(const std::string &name, Source factor, Source a, Source c)
  {
    auto &n = node("ShaderNodeMixRGB", name);
    op(n, MA_RAMP_BLEND);
    edge(factor, n, b.in(n, 0));
    edge(a, n, b.in(n, 1));
    edge(c, n, b.in(n, 2));
    return {&n};
  }
  Source build(const std::map<int, Image *> *images = nullptr)
  {
    if (!source.raw || !source.tiles)
      throw std::invalid_argument("Raw scalar UDIM graph requires an immutable sequence");
    auto &marker = node("ShaderNodeValue", "Representation");
    scalar(b.out({&marker, "Value"}), 1);
    if (check) {
      if (fill_texture_json(udim_filter_source(b.tree, prefix)) !=
          fill_texture_json(std::make_shared<const FillTexture>(source)))
        throw std::invalid_argument("Raw UDIM graph differs from its typed source");
    }
    else {
      IDPropertyTemplate empty{};
      if (!marker.prop)
        marker.prop = IDP_New(IDP_GROUP, &empty, "Raw UDIM provenance");
      const auto json =
          QJsonDocument(fill_texture_json(std::make_shared<const FillTexture>(source)).toObject())
              .toJson(QJsonDocument::Compact);
      IDP_ReplaceInGroup(marker.prop, IDP_NewString(json.constData(), "source"));
      b.changed = true;
    }
    const auto mapping_name = prefix.substr(0, prefix.find("ScalarMap")) + "Mapping";
    auto *mapping = bke::node_find_node_by_name(b.tree, mapping_name);
    if (!mapping || !STREQ(mapping->idname, "ShaderNodeMapping") || (mapping->flag & NODE_MUTED))
      throw std::invalid_argument("Raw UDIM mapping is missing");
    const auto floor = vector("Floor", NODE_VECTOR_MATH_FLOOR, {mapping, "Vector"}, {0, 0, 0});
    auto &xy = node("ShaderNodeSeparateXYZ", "FloorXY");
    edge(floor, xy, b.in(xy, "Vector"));
    // A missing member produces the native Blender UDIM sentinel (magenta), not
    // silent black/first-tile fallback. Complete mapped coverage is preflighted.
    auto &fallback = node("ShaderNodeRGB", "Missing");
    auto *rgba = static_cast<bNodeSocketValueRGBA *>(b.out({&fallback}).default_value)->value;
    const std::array<float, 4> magenta{1, 0, 1, 1};
    if (check) {
      if (!std::equal(magenta.begin(), magenta.end(), rgba))
        throw std::invalid_argument("Raw UDIM missing-tile sentinel changed");
    }
    else {
      std::copy(magenta.begin(), magenta.end(), rgba);
      BKE_ntree_update_tag_node_property(&b.tree, &fallback);
      b.changed = true;
    }
    Source result{&fallback};
    int index = 0;
    for (const auto &[tile, member] : *source.tiles) {
      const auto t = "Tile" + std::to_string(tile);
      const float x = (tile - 1001) % 10, y = (tile - 1001) / 10;
      const auto compare = [&](const char *component, float value) {
        auto &n = node("ShaderNodeMath", t + component);
        op(n, NODE_MATH_COMPARE);
        edge({&xy, component}, n, b.in(n, 0));
        scalar(b.in(n, 1), value);
        scalar(b.in(n, 2), .25f);
        return Source{&n, "Value"};
      };
      const auto gate = math(t + "Gate", NODE_MATH_MULTIPLY, compare("X", x), compare("Y", y));
      const auto local = vector(t + "UV", NODE_VECTOR_MATH_ADD, {mapping, "Vector"}, {-x, -y, 0});
      auto &image = node("ShaderNodeTexImage", t + "Image");
      if (!image.storage)
        throw std::invalid_argument("Raw UDIM image settings missing");
      auto &settings = *static_cast<NodeTexImage *>(image.storage);
      if (check) {
        if (settings.extension != SHD_IMAGE_EXTENSION_EXTEND ||
            settings.interpolation != SHD_INTERP_LINEAR || settings.projection != SHD_PROJ_FLAT ||
            !image.id)
          throw std::invalid_argument("Raw UDIM sampling changed");
      }
      else {
        if (!images || !images->contains(tile) || !images->at(tile))
          throw std::invalid_argument("Raw UDIM member is not GPU-ready");
        settings.extension = SHD_IMAGE_EXTENSION_EXTEND;
        settings.interpolation = SHD_INTERP_LINEAR;
        settings.projection = SHD_PROJ_FLAT;
        if (image.id != &images->at(tile)->id) {
          if (image.id)
            id_us_min(image.id);
          image.id = &images->at(tile)->id;
          id_us_plus(image.id);
        }
        BKE_ntree_update_tag_node_property(&b.tree, &image);
        b.changed = true;
      }
      const auto &u = settings.iuser;
      const auto &m = settings.base.tex_mapping;
      if (u.scene || u.frames != 100 || u.sfra != 1 || u.offset || u.cycl || u.multiview_eye ||
          u.pass || u.multi_index || u.view || u.layer || (u.tile != 0 && u.tile != 1001) ||
          (u.flag & ~(IMA_NEED_FRAME_RECALC | IMA_USER_FRAME_IN_RANGE)) || m.ob ||
          m.type != TEXMAP_TYPE_POINT || m.projx != PROJ_X || m.projy != PROJ_Y ||
          m.projz != PROJ_Z || m.mapping != MTEX_FLAT || (m.flag & ~TEXMAP_UNIT_MATRIX))
        throw std::invalid_argument("Raw UDIM hidden image context changed");
      for (int c = 0; c < 3; ++c)
        if (m.loc[c] || m.rot[c] || m.size[c] != 1)
          throw std::invalid_argument("Raw UDIM hidden SRT changed");
      for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
          if (m.mat[c][r] != (c == r ? 1.f : 0.f))
            throw std::invalid_argument("Raw UDIM hidden matrix changed");
      edge(local, image, b.in(image, "Vector"));
      samples.emplace(tile, &image);
      result = mix("Mix" + std::to_string(index++), gate, result, {&image});
    }
    auto &final = node("ShaderNodeVectorMath", "Mixed");
    op(final, NODE_VECTOR_MATH_ADD);
    edge(result, final, b.in(final, 0));
    vector(b.in(final, 1), {0, 0, 0});
    return {&final, "Vector"};
  }
  void audit()
  {
    auto *final = bke::node_find_node_by_name(b.tree, prefix + "Mixed");
    const auto split_name = prefix.substr(0, prefix.size() - 4) + "Split";
    auto *split = bke::node_find_node_by_name(b.tree, split_name);
    for (const auto &n : b.tree.nodes)
      if (std::string(n.name).starts_with(prefix) && !nodes.contains(const_cast<bNode *>(&n)))
        throw std::invalid_argument("Unknown raw UDIM namespace node");
    int outputs = 0;
    for (const auto &l : b.tree.links)
      if (nodes.contains(l.fromnode) || nodes.contains(l.tonode)) {
        if (edges.contains({l.fromsock, l.tosock}))
          continue;
        if (l.fromnode == final && STREQ(l.fromsock->identifier, "Vector") && split &&
            STREQ(split->idname, "ShaderNodeSeparateXYZ") && l.tonode == split &&
            l.tosock == split->inputs.first && STREQ(l.tosock->identifier, "Vector") &&
            (l.flag & NODE_LINK_VALID) && !(l.flag & NODE_LINK_MUTED))
        {
          ++outputs;
          continue;
        }
        throw std::invalid_argument("Raw UDIM graph has an unowned use");
      }
    if (outputs != 1)
      throw std::invalid_argument("Raw UDIM decoder fanout changed");
  }
  void remove(Main &main)
  {
    for (auto *n : nodes)
      bke::node_remove_node(&main, b.tree, *n, true);
    b.changed = true;
  }
};
