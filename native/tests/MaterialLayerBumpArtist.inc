// SPDX-License-Identifier: GPL-3.0-or-later
// Included only by the shipped PD149 actor TU, inside its private namespace.
class BumpArtist final : public QObject {
  using Vector = pd149_bump_test::Vector;
  QWidget *window_, *viewport_;
  QString output_, mesh_, source_, project_;
  MaterialLayeringArtistHooks hooks_;
  QTimer timer_;
  QElapsedTimer clock_;
  int stage_ = 0, frame_ = 0, ticks_ = 0, history_ = 0, input_ = 0, logged_ = -1,
      layout_index_ = 0;
  bool busy_ = false, closed_ = false, cold_ = false,
       udim_ = qEnvironmentVariableIsSet("BTS_PD149_MAP_UDIM");
  int udim_role_ = 0;
  QJsonObject neutral_, mapped_, srt_, before_, catalog_, witness_before_, aliases_;
  QJsonArray captures_, fragments_;
  QJsonObject udim_stock_;
  std::unique_ptr<pd149_bump_test::NormalEmission> emission_;
  std::function<void()> restore_view_;
  std::string view_;
  QString witness_label_;
  int witness_next_ = 0;
  static constexpr int rgb8_bound = 2;  // Fixed before the first run; analytic budget below.
  template<class T> T &get(const char *name)
  {
    auto *p = window_->findChild<T *>(name);
    require(p, (std::string("Bump actor missing actual control: ") + name).c_str());
    return *p;
  }
  const TextureSet &set() const
  {
    require(hooks_.document(), "Bump actor has no document");
    for (const auto &s : hooks_.document()->texture_sets())
      if (s.material_session_uid == hooks_.material())
        return s;
    throw std::runtime_error("Bump actor target disappeared");
  }
  Material &material()
  {
    require(G_MAIN, "Bump actor has no Main");
    for (Material &m : G_MAIN->materials)
      if (m.id.session_uid == hooks_.material())
        return m;
    throw std::runtime_error("Bump actor material missing");
  }
  QJsonObject metadata() const
  {
    return hooks_.document() ? hooks_.document()->edit_state() : QJsonObject{};
  }
  static void visible(QWidget &widget)
  {
    for (auto *p = widget.parentWidget(); p; p = p->parentWidget())
      if (auto *scroll = qobject_cast<QScrollArea *>(p)) {
        scroll->ensureWidgetVisible(&widget);
        break;
      }
    QApplication::processEvents();
    require(widget.isVisible() && widget.isEnabled() && !widget.visibleRegion().isEmpty(),
            "Bump control not visible/enabled");
  }
  void click(const char *name)
  {
    auto &button = get<QAbstractButton>(name);
    visible(button);
    button.click();
  }
  void action(const char *name)
  {
    require(!emission_ && !restore_view_, "Bump witness must restore before a document action");
    get<QAction>(name).trigger();
  }
  void number(QDoubleSpinBox &field, double value)
  {
    visible(field);
    field.setFocus();
    QKeyEvent select(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier, "a");
    QApplication::sendEvent(&field, &select);
    for (QChar c : QString::number(value, 'g', 12)) {
      QKeyEvent key(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(c));
      QApplication::sendEvent(&field, &key);
    }
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&field, &enter);
    require(std::abs(field.value() - value) < 1e-6, "Bump numeric edit did not commit");
  }
  void edit()
  {
    click("editUniformMaterialLayering");
    require(get<QDialog>("uniformMaterialLayeringDialog").isVisible(), "Bump editor not visible");
  }
  void choose(int input)
  {
    get<QComboBox>("uniformMaterialInput").setCurrentIndex(input);
    auto &channel = get<QComboBox>("materialInputSourceChannel");
    require(channel.findData(3) < 0 && channel.findData(4) >= 0,
            "Normal must stay hidden and Bump must be available");
    channel.setCurrentIndex(channel.findData(4));
    require(!get<QComboBox>("materialInputColorSpace").isVisible(),
            "Bump must not offer a color interpretation");
  }
  void file()
  {
    auto &button = get<QPushButton>("materialInputFile");
    visible(button);
    QTimer::singleShot(0, this, [this] {
      auto *d = window_->findChild<QFileDialog *>("materialInputFileDialog");
      require(d && d->isVisible(), "Bump real file dialog missing");
      d->selectFile(source_);
      QMetaObject::invokeMethod(d, "accept", Qt::DirectConnection);
    });
    button.click();
  }
  void shelf()
  {
    click("materialInputShelf");
    auto &list = get<QListWidget>("shelfAssets");
    QListWidgetItem *found = nullptr;
    for (int i = 0; i < list.count(); ++i)
      if (list.item(i)->text().contains("bump-source")) {
        found = list.item(i);
        break;
      }
    require(found, "Bump original missing from actual Shelf");
    list.setCurrentItem(found);
    list.scrollToItem(found);
    visible(list);
    const auto point = list.visualItemRect(found).center();
    for (auto kind : {QEvent::MouseButtonPress,
                      QEvent::MouseButtonRelease,
                      QEvent::MouseButtonDblClick,
                      QEvent::MouseButtonRelease})
    {
      QMouseEvent e(kind,
                    point,
                    list.viewport()->mapToGlobal(point),
                    Qt::LeftButton,
                    kind == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                    Qt::NoModifier);
      QApplication::sendEvent(list.viewport(), &e);
    }
    require(hooks_.state()["texture_loading"].toBool(),
            "Bump Shelf did not use shared worker loader");
  }
  QJsonObject aliases()
  {
    const auto address = [](const void *value) {
      const auto result = pointer(value);
      require(result.size() == 1 && result.at(0).isString() &&
                  !result.at(0).toString().isEmpty() && result.at(0).toString() != "0",
              "Bump source pointer must be one nonzero hexadecimal string");
      return result.at(0);
    };
    QJsonObject value;
    for (auto &n : material().nodetree->nodes)
      if (STREQ(n.idname, "ShaderNodeTexImage") && n.id &&
          STRPREFIX(n.name, "__BTS_LayerMaterial"))
      {
        auto *image = reinterpret_cast<Image *>(n.id);
        const auto gpu = BKE_image_gpu_override_texture(image);
        value[n.name] = QJsonArray{address(image), address(gpu.texture ? *gpu.texture : nullptr)};
      }
    return value;
  }
  bool loaded()
  {
    return !hooks_.state()["texture_loading"].toBool() &&
           get<QLabel>("materialInputSource").text().contains("bump-source");
  }
  static void source_file(const QString &path, int source_tile = 0)
  {
    // Same existing original-backed EXR writer used by the CPU codec fixture.
    std::unique_ptr<Main, decltype(&BKE_main_free)> main(BKE_main_new(), BKE_main_free);
    auto *image = reinterpret_cast<Image *>(BKE_id_new(main.get(), ID_IM, "Own affine Bump EXR"));
    require(image, "Bump fixture Image allocation failed");
    image->source = IMA_SRC_GENERATED;
    image->type = IMA_TYPE_UV_TEST;
    image->alpha_mode = IMA_ALPHA_CHANNEL_PACKED;
    image->flag |= IMA_HIGH_BITDEPTH;
    auto *tile = static_cast<ImageTile *>(image->tiles.first);
    require(tile, "Bump fixture tile missing");
    tile->gen_x = tile->gen_y = 128;
    image->runtime->gpuflag |= IMA_HOSTED_IMMUTABLE_SOURCE;
    FillTexture texture;
    texture.asset = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    texture.hash = std::string(64, 'b');
    texture.name = "Owned affine Bump";
    texture.raw = true;
    texture.width = texture.height = 128;
    auto pixels = std::make_shared<std::vector<FillTexture::Pixel>>(128 * 128);
    for (int y = 0; y < 128; ++y)
      for (int x = 0; x < 128; ++x) {
        const float h = .5f + (source_tile ? float(-32 + 64 * (source_tile - 1001)) : 0.f) +
                        64.f * (x + .5f) / 128 + 32.f * (y + .5f) / 128;
        (*pixels)[y * 128 + x] = {h, h, h, 0};
      }
    texture.pixels = std::move(pixels);
    std::unique_ptr<ImBuf, decltype(&IMB_freeImBuf)> header(
        material_layer_source_pixel_view(texture), IMB_freeImBuf);
    require(BKE_image_assign_hosted_immutable_source(image, 1001, header.get()) &&
                BKE_image_memorypack(image),
            "Bump fixture original EXR encoding failed");
    const auto *part = static_cast<ImagePackedFile *>(image->packedfiles.first);
    require(part && part->packedfile && part->packedfile->size > 0,
            "Bump fixture packed bytes missing");
    QFile f(path);
    require(f.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                f.write(static_cast<const char *>(part->packedfile->data),
                        part->packedfile->size) == part->packedfile->size,
            "Bump fixture original write failed");
  }
  static double sample(const LayerMaterialInput &input, double u, double v, int role = 4)
  {
    if (!input.textures[role])
      return role == 1 ? input.values.metallic :
             role == 2 ? input.values.roughness :
             role == 5 ? input.values.ambient_occlusion :
                         .5;
    const auto uv = layered_material_uv(input, {u, v});
    const bool udim = bool(input.textures[role]->tiles);
    const int tx = int(std::floor(uv[0])), ty = int(std::floor(uv[1])), tile = 1001 + tx + 10 * ty;
    if (udim)
      require(tx >= 0 && tx < 10 && ty >= 0 && input.textures[role]->tiles->contains(tile),
              "Independent raw UDIM reference left its supplied tile domain");
    const double x = (uv[0] - (udim ? tx : 0)) * 128 - .5,
                 y = (uv[1] - (udim ? ty : 0)) * 128 - .5;
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double fx = std::round((x - ix) * 256) / 256, fy = std::round((y - iy) * 256) / 256;
    double h = 0;
    for (int j = 0; j < 2; ++j)
      for (int i = 0; i < 2; ++i) {
        const int px = udim ? std::clamp(ix + i, 0, 127) : ((ix + i) % 128 + 128) % 128,
                  py = udim ? std::clamp(iy + j, 0, 127) : ((iy + j) % 128 + 128) % 128;
        h += (.5 + (udim ? -32 + 64 * (tile - 1001) : 0) + 64 * (px + .5) / 128 +
              32 * (py + .5) / 128) *
             (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
      }
    return h;
  }
  std::array<double, 2> slopes() const
  {
    const auto &input = set().material_layering.materials[input_];
    if (!input.textures[4])
      return {0, 0};
    const double angle = input.rotation * 3.14159265358979323846 / 180;
    return {input.tiling[0] * (64 * std::cos(angle) + 32 * std::sin(angle)),
            input.tiling[1] * (-64 * std::sin(angle) + 32 * std::cos(angle))};
  }
  void height(const char *label)
  {
    require(!emission_, "Height sampling must precede fragment overlay");
    for (const auto &input : set().material_layering.materials)
      require(!input.textures[3] && input.values.normal == (std::array<float, 3>{0, 0, 1}) &&
                  input.values.bump == .5f,
              "Bump route changed hidden Normal or uniform height contract");
    QJsonArray rows;
    double maximum = 0;
    const auto counts = PaintPixels::recipe_materializations();
    hooks_.host.with_gpu([&] {
      pd149_sources_test::SourceGpuTap tap(
          material(), pd149_sources_test::Output::Height, 128, 128);
      std::array<std::unique_ptr<GpuTap>, 3> masks;
      for (int i = 0; i < 3; ++i)
        masks[i] = std::make_unique<GpuTap>(material(), Output(int(Output::Mask0) + i), 128, 128);
      for (int tile : {1001, 1002}) {
        const auto values = tap.sample(tile);
        std::array<std::vector<std::array<float, 4>>, 3> mask;
        for (int i = 0; i < 3; ++i)
          mask[i] = masks[i]->sample(tile);
        require(values.size() == 128 * 128, "Bump Height packet shape");
        for (int y : {19, 51, 83, 115})
          for (int x : {13, 45, 77, 109}) {
            const int at = y * 128 + x;
            std::array<double, 3> opacity{};
            for (int i = 0; i < 3; ++i) {
              require(mask[i].size() == 128 * 128, "Bump opacity packet shape");
              opacity[i] = std::clamp(double(mask[i][at][0]), 0., 1.);
              require(opacity[i] == double(i == 0 && input_ == 1),
                      "Bump derivative fixture needs constant zero/one opacity");
              for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                  require(mask[i][at + dy * 128 + dx][0] == opacity[i],
                          "Bump derivative fixture opacity is not locally constant");
            }
            const auto weights = layered_material_weights(
                {float(opacity[0]), float(opacity[1]), float(opacity[2])});
            double expected = .5;
            for (int i = 0; i < 4; ++i)
              expected += weights[i] * sample(set().material_layering.materials[i],
                                              tile - 1001 + (x + .5) / 128.,
                                              (y + .5) / 128.);
            const double actual = values[at][0];
            require(std::isfinite(actual) && std::isfinite(expected),
                    "Bump Height nonfinite value");
            maximum = std::max(maximum, std::abs(actual - expected));
            rows.append(QJsonObject{{"tile", tile},
                                    {"xy", QJsonArray{x, y}},
                                    {"actual", QJsonArray{actual}},
                                    {"independent", expected}});
          }
      }
    });
    if (udim_)
      scalar_roles(label);
    captures_.append(QJsonObject{{"stage", label},
                                 {"components", rows.size()},
                                 {"maximum_error", maximum},
                                 {"fixed_tolerance", 3e-5},
                                 {"probes", rows}});
    write(false);
    require(PaintPixels::recipe_materializations() == counts && maximum < 3e-5,
            "Bump raw source Height exceeds unchanged 3e-5 or materialized CPU paint");
  }
  std::array<double, 3> display(Vector normal)
  {
    std::array<float, 4> rgba{
        float(normal[0] * .5 + .5), float(normal[1] * .5 + .5), float(normal[2] * .5 + .5), 1};
    hooks_.host.display_rgba(rgba);
    std::array<double, 3> result;
    for (int c = 0; c < 3; ++c) {
      require(std::isfinite(rgba[c]), "Bump display reference is nonfinite");
      result[c] = std::clamp(double(rgba[c]), 0., 1.) * 255;
    }
    return result;
  }
  void begin_witness(const char *label, int next)
  {
    require(hooks_.begin_fragment_view && hooks_.scene_point && !emission_ && !restore_view_,
            "Bump fragment adapters unavailable");
    witness_label_ = label;
    witness_next_ = next;
    witness_before_ = metadata();
    layout_index_ = get<QComboBox>("viewLayoutControl").currentIndex();
    view_ = hooks_.host.color_state()["view_transform"].toString().toStdString();
    restore_view_ = hooks_.begin_fragment_view();
    hooks_.host.set_display_transform("Standard");
    const auto color = hooks_.host.color_state();
    require(color["display"] == "sRGB" && color["look"] == "None" && color["exposure"] == 0 &&
                color["gamma"] == 1,
            "Bump witness requires pinned Standard/sRGB without look/exposure/gamma changes");
    require(get<QComboBox>("viewLayoutControl").currentIndex() == 0 &&
                hooks_.host.inspection_mode() == InspectionMode::Material,
            "Bump fragment witness is not actual SceneOnly material view");
    hooks_.host.with_gpu([&] {
      emission_ = std::make_unique<pd149_bump_test::NormalEmission>(*G_MAIN, material());
    });
    frame_ = hooks_.state()["render_frames"].toInt() + 8;
    stage_ = 90;
    viewport_->update();
  }
  void restore_witness()
  {
    if (emission_)
      hooks_.host.with_gpu([&] {
        emission_->restore();
        emission_.reset();
      });
    if (restore_view_) {
      hooks_.host.set_display_transform(view_);
      auto restore = std::move(restore_view_);
      restore_view_ = {};
      restore();
      require(get<QComboBox>("viewLayoutControl").currentIndex() == layout_index_ &&
                  metadata() == witness_before_,
              "Bump fragment witness failed to restore layout/document");
    }
  }
  void fragment()
  {
    require(bool(emission_), "Bump fragment overlay missing");
    emission_->verify();
    // The analytic q8 bound is for the existing single-mip raw source path.
    // Reject any changed source/sampler contract before comparing framebuffer data.
    if (udim_) {
      hooks_.host.with_gpu([&] {
        if (const auto &source = set().material_layering.materials[input_].textures[4]) {
          require(bool(source->tiles), "UDIM witness source is not a sequence");
          for (const auto &[tile, node] :
               MaterialLayerShader::raw_udim_samples(*G_MAIN, material(), input_, 4, *source))
          {
            auto *image = reinterpret_cast<Image *>(node->id);
            const auto gpu = BKE_image_gpu_override_texture(image);
            auto *texture = gpu.texture ? *gpu.texture : nullptr;
            require(texture && GPU_texture_width(texture) == 128 &&
                        GPU_texture_height(texture) == 128 && GPU_texture_mip_count(texture) == 1,
                    "UDIM Bump witness needs original128 single-mip sources");
          }
        }
      });
    }
    else {
      hooks_.host.with_gpu([&] {
        const auto &input = set().material_layering.materials[input_];
        if (input.textures[4]) {
          const auto &source = *input.textures[4];
          const auto name = "__BTS_LayerMaterial" + std::to_string(input_) + "ScalarMap4";
          auto *node = blender::bke::node_find_node_by_name(*material().nodetree, name.c_str());
          require(node && STREQ(node->idname, "ShaderNodeTexImage") && node->id && node->storage,
                  "Bump sampler node missing");
          const auto *settings = static_cast<const blender::NodeTexImage *>(node->storage);
          auto *image = reinterpret_cast<blender::Image *>(node->id);
          const auto cached = BKE_image_gpu_override_texture(image);
          auto *texture = cached.texture ? *cached.texture : nullptr;
          require(source.raw && source.width == 128 && source.height == 128 &&
                      image->alpha_mode == IMA_ALPHA_CHANNEL_PACKED &&
                      settings->interpolation == SHD_INTERP_LINEAR &&
                      settings->extension == SHD_IMAGE_EXTENSION_REPEAT && texture &&
                      GPU_texture_width(texture) == 128 && GPU_texture_height(texture) == 128 &&
                      GPU_texture_mip_count(texture) == 1,
                  "Bump bound requires the unchanged raw128 LINEAR/REPEAT single-mip source");
        }
      });
    }
    using namespace pd149_bump_test;
    const auto frame = capture_viewport_framebuffer(viewport_).convertToFormat(
        QImage::Format_RGBA8888);
    require(!frame.isNull() && emission_, "Bump fragment framebuffer missing");
    require(frame.save(output_ + "/bump-fragment-" + witness_label_ + ".png"),
            "Bump raw fragment frame save failed");
    require(get<QComboBox>("viewLayoutControl").currentIndex() == 0,
            "Bump capture left SceneOnly");
    const auto scene = hooks_.host.scene_snapshot();
    const auto gradient = slopes();
    QJsonArray rows;
    double maximum = 0;
    bool budget_ok = true, signal_ok = true;
    int components = 0;
    const double sx = double(frame.width()) / viewport_->width(),
                 sy = double(frame.height()) / viewport_->height();
    for (int tile : {1001, 1002}) {
      const UvFaceTriangle *triangle = nullptr;
      for (const auto &candidate : scene.uv_triangles) {
        if (scene.materials.at(candidate.material).session_id != hooks_.material())
          continue;
        double u = 0, v = 0;
        for (const auto &p : candidate.corners) {
          u += p[0] / 3.;
          v += p[1] / 3.;
        }
        if (1001 + int(std::floor(u)) + 10 * int(std::floor(v)) == tile) {
          triangle = &candidate;
          break;
        }
      }
      require(triangle, "Bump actual evaluated triangle missing");
      std::array<Vector, 3> world;
      std::array<std::array<double, 2>, 3> uv;
      std::array<QPointF, 3> screen;
      for (int i = 0; i < 3; ++i) {
        for (int c = 0; c < 3; ++c)
          world[i][c] = triangle->positions[i][c];
        for (int c = 0; c < 2; ++c)
          uv[i][c] = triangle->corners[i][c];
        const auto p = hooks_.scene_point(world[i]);
        screen[i] = {p.x() * sx, p.y() * sy};
      }
      const auto metric = plane(world, uv);
      require(triangle->has_normals, "Bump witness requires actual flat fixture normals");
      for (const auto &n : triangle->normals)
        require(dot(normalized(Vector{n[0], n[1], n[2]}), metric.normal) > 1 - 1e-6,
                "Bump witness excludes smooth/nonflat geometry");
      const auto a = screen[1] - screen[0], b = screen[2] - screen[0];
      const double det = a.x() * b.y() - b.x() * a.y();
      require(det < -1e-6, "Bump witness needs a visible front-facing orthographic triangle");
      const QPoint at(int(std::floor((screen[0].x() + screen[1].x() + screen[2].x()) / 3)),
                      int(std::floor((screen[0].y() + screen[1].y() + screen[2].y()) / 3)));
      // Fixed corner exclusion keeps 3D/material labels and bottom-right axes out;
      // the scoped main adapter disables paint/stencil/path/skew/symmetry/cursor.
      require(at.x() > 52 * sx && at.y() > 42 * sy && at.x() < frame.width() - 100 * sx &&
                  at.y() < frame.height() - 90 * sy,
              "Bump interior overlaps viewport chrome");
      const double u1 = uv[1][0] - uv[0][0], u2 = uv[2][0] - uv[0][0], v1 = uv[1][1] - uv[0][1],
                   v2 = uv[2][1] - uv[0][1];
      const double ux = (u1 * b.y() - u2 * a.y()) / det, uy = (-u1 * b.x() + u2 * a.x()) / det,
                   vx = (v1 * b.y() - v2 * a.y()) / det, vy = (-v1 * b.x() + v2 * a.x()) / det,
                   jac = ux * vy - uy * vx;
      require(std::abs(jac) > 1e-12, "Bump screen/UV metric degenerate");
      const auto normal = response(
                     metric, gradient[0], gradient[1], emission_->distance, emission_->strength),
                 expected = display(normal), flat = display(metric.normal);
      double signal = 0, display_uncertainty = 0;
      // Original texel steps are exactly .5 and .25. Nearest-1/256 fractions
      // give <=(.5+.25)/512 height error per lookup, twice that per difference.
      // Transform the differential uncertainty through the ACTUAL screen metric.
      const double dh = (std::abs(gradient[0]) + std::abs(gradient[1])) > 0 ?
                            (.5 + .25) / 256 / emission_->filter_width :
                            0;

      // Analytic conservative bound, not extrema inferred from sampled output.
      // G is linear in dH/du,dH/dv. Its screen-noise rectangle has a convex norm,
      // so the maximum gradient-error norm is attained at a rectangle vertex.
      const double aa = dot(metric.du, metric.du), bb = dot(metric.du, metric.dv),
                   cc = dot(metric.dv, metric.dv), gg = aa * cc - bb * bb;
      const auto A = scale(add(scale(metric.du, cc), scale(metric.dv, -bb)), 1 / gg),
                 B = scale(add(scale(metric.dv, aa), scale(metric.du, -bb)), 1 / gg);
      const auto dx = scale(add(scale(A, vy), scale(B, -uy)), 1 / jac),
                 dy = scale(add(scale(A, -vx), scale(B, ux)), 1 / jac),
                 G = add(scale(A, gradient[0]), scale(B, gradient[1]));
      double gradient_error = 0;
      for (int bits = 0; bits < 4; ++bits) {
        const auto error = add(scale(dx, (bits & 1) ? dh : -dh), scale(dy, (bits & 2) ? dh : -dh));
        gradient_error = std::max(gradient_error, std::sqrt(dot(error, error)));
      }
      const double D = emission_->distance, K = std::sqrt(dot(G, G)) + gradient_error;
      const auto unnormalized = add(metric.normal, scale(G, -D));
      // Pinned Standard/sRGB is a linear scene->Rec709 matrix then the monotonic
      // inverse ExponentWithLinear curve (config.ocio:133-147,310-312,517-528).
      // Over a positive interval above .01, its derivative decreases with x.
      // Bound each matrix-row projection of normalized(N-DG) analytically; |N-DG|
      // is >=1 since G is tangent, hence all denominators below are bounded by1.
      std::array<Vector, 3> matrix{};
      for (int c = 0; c < 3; ++c) {
        float unit[3]{}, converted[3];
        unit[c] = 1;
        IMB_colormanagement_scene_linear_to_rec709(converted, unit);
        for (int r = 0; r < 3; ++r)
          matrix[r][c] = converted[r];
      }
      for (int r = 0; r < 3; ++r) {
        const auto m = matrix[r];
        const double row_noise = dh * (std::abs(dot(m, dx)) + std::abs(dot(m, dy)));
        const double normal_error = D * row_noise +
                                    D * D * (std::abs(dot(m, unnormalized)) + D * row_noise) * K *
                                        gradient_error;
        const double half_float = (std::abs(m[0]) + std::abs(m[1]) + std::abs(m[2])) / 2048;
        const double linear_error = .5 * normal_error + half_float;
        const double linear = dot(m, add(scale(normal, .5), Vector{.5, .5, .5})),
                     lower = linear - linear_error, upper = linear + linear_error;
        // The final UNORM8 conversion is identically0/1 when the WHOLE linear
        // interval is clipped; its derivative is then exactly0, including negative
        // Rec709 channels produced by the AP1 matrix. Unproved toe-crossing cases
        // remain ineligible. This is an input-derived bound, never fitted to pixels.
        const bool clipped = upper <= 0 || lower >= 1;
        budget_ok &= std::isfinite(lower) && std::isfinite(upper) && (clipped || lower > .01);
        const double slope = clipped ? 0 :
                                       1.055 / 2.4 * std::pow(std::max(lower, .01), 1 / 2.4 - 1);
        display_uncertainty = std::max(display_uncertainty, 255 * slope * linear_error);
        const double analytic =
            std::clamp(1.055 * std::pow(std::max(linear, 0.), 1 / 2.4) - .055, 0., 1.) * 255;
        budget_ok &= std::abs(analytic - expected[r]) <=
                     .05;  // known Standard CPU/config contract
      }
      // 1/2048 encoded-RGB reserve covers half storage/float arithmetic. Dither is
      // disabled/restored by the scoped witness. One code covers GPU UNORM8 and
      // reference integer rounding; .05 covers the pinned transfer approximation.
      // The derived budget MUST fit the fixed2 codes, never grow after a failure.
      const double derived_bound = std::ceil(display_uncertainty + 1 + .05);
      budget_ok &= derived_bound <= rgb8_bound;
      for (int c = 0; c < 3; ++c)
        signal = std::max(signal, std::abs(double(qRound(expected[c])) - qRound(flat[c])));
      const bool nonflat = gradient[0] != 0 || gradient[1] != 0;
      signal_ok &= !nonflat || signal > 2 * rgb8_bound;
      QJsonArray pixels;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          const QPoint p = at + QPoint(dx, dy);
          const auto delta = QPointF(p) + QPointF(.5, .5) - screen[0];
          const double w1 = (delta.x() * b.y() - b.x() * delta.y()) / det,
                       w2 = (a.x() * delta.y() - delta.x() * a.y()) / det;
          require(w1 > .1 && w2 > .1 && w1 + w2 < .9,
                  "Bump sample patch touches an actual triangle edge");
          double actual_u = uv[0][0] + w1 * u1 + w2 * u2, actual_v = uv[0][1] + w1 * v1 + w2 * v2;
          const auto &source_input = set().material_layering.materials[input_];
          if (source_input.textures[4]) {
            const auto q = layered_material_uv(source_input, {actual_u, actual_v});
            const double angle = source_input.rotation * 3.14159265358979323846 / 180,
                         ct = std::cos(angle), st = std::sin(angle);
            for (int axis = 0; axis < 2; ++axis) {
              const auto derivative = [&](double u, double v) {
                return axis == 0 ?
                           ct * source_input.tiling[0] * u - st * source_input.tiling[1] * v :
                           st * source_input.tiling[0] * u + ct * source_input.tiling[1] * v;
              };
              const double footprint = 2. / 128 +
                                       emission_->filter_width * (std::abs(derivative(ux, vx)) +
                                                                  std::abs(derivative(uy, vy))) +
                                       std::abs(derivative(ux, vx)) + std::abs(derivative(uy, vy));
              const double fraction = q[axis] - std::floor(q[axis]);
              require(fraction > footprint && fraction < 1 - footprint,
                      "Bump sample/filter footprint crosses a repeating ramp discontinuity");
            }
          }
          const auto rgba = frame.pixelColor(p);
          require(rgba.alpha() == 255, "Bump fragment witness is not opaque");
          QJsonArray actual;
          for (int c = 0; c < 3; ++c) {
            const int value = c == 0 ? rgba.red() : c == 1 ? rgba.green() : rgba.blue();
            actual.append(value);
            ++components;
          }
          pixels.append(QJsonObject{{"xy", QJsonArray{p.x(), p.y()}}, {"actual_rgb8", actual}});
        }
      rows.append(
          QJsonObject{{"tile", tile},
                      {"triangle", qint64(triangle->triangle_id)},
                      {"dPdu", QJsonArray{metric.du[0], metric.du[1], metric.du[2]}},
                      {"dPdv", QJsonArray{metric.dv[0], metric.dv[1], metric.dv[2]}},
                      {"screen_uv_differentials", QJsonArray{ux, uy, vx, vy}},
                      {"normal", QJsonArray{normal[0], normal[1], normal[2]}},
                      {"expected_rgb8",
                       QJsonArray{qRound(expected[0]), qRound(expected[1]), qRound(expected[2])}},
                      {"flat_rgb8", QJsonArray{qRound(flat[0]), qRound(flat[1]), qRound(flat[2])}},
                      {"signal_codes", signal},
                      {"derived_bound_codes", derived_bound},
                      {"pixels", pixels}});
    }
    // Every geometry, sampler-footprint and analytic-budget precondition must
    // hold before the first comparison of the observed GPU components.
    const bool compare = budget_ok && signal_ok;
    if (compare)
      for (const auto &value : rows) {
        const auto row = value.toObject();
        const auto expected = row["expected_rgb8"].toArray();
        for (const auto &pixel : row["pixels"].toArray()) {
          const auto actual = pixel.toObject()["actual_rgb8"].toArray();
          for (int c = 0; c < 3; ++c)
            maximum = std::max(maximum, std::abs(actual[c].toDouble() - expected[c].toDouble()));
        }
      }
    const auto label = witness_label_;
    const QJsonObject packet{{"stage", label},
                             {"scene_only", true},
                             {"fragment_not_compute", true},
                             {"view_transform", "Standard"},
                             {"display_state", hooks_.host.color_state()},
                             {"display_dither_diagnostic_only", 0},
                             {"distance", emission_->distance},
                             {"strength", emission_->strength},
                             {"filter_width", emission_->filter_width},
                             {"components", components},
                             {"maximum_rgb8_error_codes", maximum},
                             {"fixed_rgb8_bound", rgb8_bound},
                             {"numeric_comparison_performed", compare},
                             {"budget_precondition", budget_ok},
                             {"nonflat_signal_precondition", signal_ok},
                             {"rows", rows}};
    fragments_.append(packet);
    const bool saved = frame.save(output_ + "/bump-fragment-" + label + ".png");
    write(false);
    restore_witness();
    {
      auto restored = fragments_.last().toObject();
      restored["surface_layout_document_restored"] = true;
      fragments_[fragments_.size() - 1] = restored;
      write(false);
    }
    require(saved && budget_ok && signal_ok && components == 54,
            "Bump fragment witness fixture/budget/signal precondition failed");
    require(maximum <= rgb8_bound,
            "Actual EEVEE Bump Normal disagrees with independent derivative/display oracle");
    stage_ = witness_next_;
  }
  QJsonArray udim_scalars_;
  void scalar_roles(const char *label)
  {
    QJsonArray rows;
    double maximum = 0;
    hooks_.host.with_gpu([&] {
      std::array<std::unique_ptr<pd149_sources_test::SourceGpuTap>, 3> taps;
      for (int i = 0; i < 3; ++i)
        taps[i] = std::make_unique<pd149_sources_test::SourceGpuTap>(
            material(),
            std::array<pd149_sources_test::Output, 3>{pd149_sources_test::Output::Metallic,
                                                      pd149_sources_test::Output::Roughness,
                                                      pd149_sources_test::Output::AO}[i],
            128,
            128);
      for (int tile : {1001, 1002}) {
        std::array<std::vector<std::array<float, 4>>, 3> values;
        for (int c = 0; c < 3; ++c) {
          values[c] = taps[c]->sample(tile);
          require(values[c].size() == 128 * 128, "Raw UDIM scalar packet shape");
        }
        for (int y : {19, 51, 83, 115})
          for (int x : {13, 45, 77, 109}) {
            QJsonArray actual, expected;
            for (int c = 0; c < 3; ++c) {
              const int role = std::array<int, 3>{1, 2, 5}[c];
              const auto &input = set().material_layering.materials[input_];
              const double want = input.textures[role] ?
                                      std::clamp(sample(input,
                                                        tile - 1001 + (x + .5) / 128.,
                                                        (y + .5) / 128.,
                                                        role),
                                                 0.,
                                                 1.) :
                                      (role == 1 ? input.values.metallic :
                                       role == 2 ? input.values.roughness :
                                                   input.values.ambient_occlusion);
              const double observed = values[c][y * 128 + x][0];
              require(std::isfinite(observed) && std::isfinite(want), "Raw UDIM scalar nonfinite");
              maximum = std::max(maximum, std::abs(observed - want));
              actual.append(observed);
              expected.append(want);
            }
            rows.append(QJsonObject{{"tile", tile},
                                    {"xy", QJsonArray{x, y}},
                                    {"actual", actual},
                                    {"independent", expected}});
          }
      }
    });
    udim_scalars_.append(QJsonObject{{"stage", label},
                                     {"components", 96},
                                     {"maximum_error", maximum},
                                     {"fixed_tolerance", 3e-5},
                                     {"probes", rows}});
    write(false);
    require(maximum < 3e-5, "Raw UDIM scalar oracle exceeds unchanged3e-5");
  }
  void capture_udim_stock()
  {
    auto *const live = G_MAIN;
    ReportList reports{};
    BKE_reports_init(&reports, RPT_STORE);
    const auto clean = qScopeGuard([&] { BKE_reports_free(&reports); });
    BlendFileReadReport report{};
    report.reports = &reports;
    std::unique_ptr<BlendFileData, decltype(&BLO_blendfiledata_free)> file(
        BLO_read_from_file(project_.toUtf8().constData(), BLO_READ_SKIP_USERDEF, &report),
        BLO_blendfiledata_free);
    require(file && file->main && file->curscene && G_MAIN == live,
            "Raw UDIM stock read changed live Main");
    auto &main = *file->main;
    auto &scene = *file->curscene;
    const auto property = [](IDProperty *root, const char *key, int type) {
      auto *value = root ? IDP_GetPropertyFromGroup(root, key) : nullptr;
      require(value && value->type == type, "Raw UDIM stock metadata missing");
      return value;
    };
    const auto json = QJsonDocument::fromJson(
                          QByteArray(IDP_string_get(
                              property(scene.id.properties, "bts_document", IDP_STRING))))
                          .object();
    auto *stored_sets = property(scene.id.properties, "bts_sets", IDP_GROUP);
    std::vector<TextureSetBinding> bindings;
    Material *owner = nullptr;
    for (const auto &value : json["texture_sets"].toArray()) {
      const auto row = value.toObject();
      const QUuid id(row["id"].toString());
      auto *target = IDP_ID_get(
          property(stored_sets, id.toString(QUuid::WithoutBraces).toUtf8().constData(), IDP_ID));
      Material *owned = nullptr;
      for (Material &m : main.materials)
        if (&m.id == target)
          owned = &m;
      require(owned, "Raw UDIM stock material outside Main");
      bindings.push_back({id, target->session_uid, target->name + 2});
      if (id == set().id)
        owner = owned;
    }
    require(owner, "Raw UDIM stock selected owner missing");
    auto document = TextureDocument::from_json(json, bindings);
    ProjectResourceImages resources;
    const auto assets = read_project_assets(scene, &resources);
    resolve_fill_textures(document, assets);
    require(validate_packed_material_layer_sources(main, scene, document, resources) &&
                assets.size() == 1 && resources.by_key.size() == 2,
            "Raw UDIM stock graph/member provenance failed");
    pd149_sources_test::StockSavedImages audit;
    int payloads = 0, aliases = 0;
    std::set<std::string> hashes;
    for (Image &image : main.images) {
      aliases += bool(image.runtime->gpuflag & IMA_HOSTED_IMMUTABLE_SOURCE) ||
                 STRPREFIX(image.id.name + 2, "BTS Layer Source ");
      for (const auto &[key, pointer] : resources.by_key)
        if (pointer == &image) {
          audit.audited.insert(&image);
          require(image.flag & IMA_HIGH_BITDEPTH, "Raw UDIM stock fullfloat flag missing");
          for (const ImagePackedFile &part : image.packedfiles) {
            require(part.packedfile && part.packedfile->data,
                    "Raw UDIM stock packed bytes absent");
            ++payloads;
            hashes.insert(QCryptographicHash::hash(
                              QByteArrayView(static_cast<const char *>(part.packedfile->data),
                                             part.packedfile->size),
                              QCryptographicHash::Sha256)
                              .toHex()
                              .toStdString());
          }
        }
    }
    require(payloads == 2 && hashes.size() == 2 && aliases == 0,
            "Raw UDIM stock duplicated encodings or retained aliases");
    for (const auto &node : owner->nodetree->nodes)
      if (node.id && STREQ(node.idname, "ShaderNodeTexImage") &&
          (STRPREFIX(node.name, "__BTS_UserPixels") || STREQ(node.name, "__BTS_AOPixels")))
        for (Image &image : main.images)
          if (&image.id == node.id)
            audit.audited.insert(&image);
    // Height/Metal/Rough retain their ordinary Main branch even when its
    // coverage is zero. Admit exactly its native saved channels, never all Images.
    const auto &saved_set = document.set(owner->id.session_uid);
    require(saved_set.id == set().id && saved_set.tiles == set().tiles,
            "Raw UDIM stock set ownership/tiles differ");
    std::set<Image *> composites;
    QJsonArray composite_rows;
    for (int role : {1, 2, 4}) {
      const auto suffix = std::to_string(role);
      auto &tree = *owner->nodetree;
      auto *texture = bke::node_find_node_by_name(tree, ("__BTS_Pixels" + suffix).c_str()),
           *divide = bke::node_find_node_by_name(tree, ("__BTS_Unpremultiply" + suffix).c_str()),
           *mix = bke::node_find_node_by_name(tree, ("__BTS_BaseMix" + suffix).c_str());
      require(texture && divide && mix && STREQ(texture->idname, "ShaderNodeTexImage") &&
                  STREQ(divide->idname, "ShaderNodeVectorMath") &&
                  STREQ(mix->idname, "ShaderNodeMixRGB") && !(texture->flag & NODE_MUTED) &&
                  !(divide->flag & NODE_MUTED) && !(mix->flag & NODE_MUTED) &&
                  divide->custom1 == NODE_VECTOR_MATH_DIVIDE && mix->custom1 == MA_RAMP_BLEND,
              "Raw UDIM stock canonical composite nodes changed");
      const auto edge = [&](bNode &from, const char *output, bNode &to, const char *input) {
        const auto &link = pd149_test::incoming(tree, pd149_test::input(to, input));
        require(link.fromnode == &from && link.fromsock == &pd149_test::output(from, output) &&
                    link.tonode == &to,
                "Raw UDIM stock canonical composite edge changed");
      };
      edge(*texture, "Color", *divide, "Vector");
      edge(*texture, "Alpha", *divide, "Vector_001");
      edge(*texture, "Alpha", *mix, "Fac");
      edge(*divide, "Vector", *mix, "Color2");
      Image *image = nullptr;
      for (Image &candidate : main.images)
        if (&candidate.id == texture->id)
          image = &candidate;
      require(image && composites.insert(image).second && image->source == IMA_SRC_TILED &&
                  image->type == IMA_TYPE_IMAGE && image->alpha_mode == IMA_ALPHA_CHANNEL_PACKED &&
                  !(image->runtime->gpuflag & (IMA_HOSTED_GPU_OVERRIDE | IMA_HOSTED_CPU_MIRROR |
                                               IMA_HOSTED_IMMUTABLE_SOURCE)),
              "Raw UDIM stock composite Image ownership/role/descriptor changed");
      const auto *space = IMB_colormanagement_space_get_named(image->colorspace_settings.name);
      require(space && IMB_colormanagement_space_is_data(space),
              "Raw UDIM stock scalar composite is not data");
      std::set<int> declared, packed;
      bool generated_descriptors = true;
      QJsonArray tile_rows, packed_rows;
      for (const ImageTile &tile : image->tiles) {
        require(declared.insert(tile.tile_number).second &&
                    saved_set.tiles.contains(tile.tile_number) &&
                    tile.gen_x == saved_set.width(tile.tile_number) &&
                    tile.gen_y == saved_set.rows(tile.tile_number),
                "Raw UDIM stock composite declared tile/dimensions changed");
        QJsonArray color;
        for (float value : tile.gen_color) {
          generated_descriptors &= std::isfinite(value);
          color.append(std::isfinite(value) ? QJsonValue(value) : QJsonValue());
        }
        generated_descriptors &= (tile.gen_flag & (IMA_GEN_FLOAT | IMA_GEN_TILE)) ==
                                     (IMA_GEN_FLOAT | IMA_GEN_TILE) &&
                                 tile.gen_type == IMA_GENTYPE_BLANK && tile.gen_depth == 32;
        tile_rows.append(QJsonObject{{"tile", tile.tile_number},
                                     {"width", tile.gen_x},
                                     {"height", tile.gen_y},
                                     {"depth", tile.gen_depth},
                                     {"type", tile.gen_type},
                                     {"flags", tile.gen_flag},
                                     {"gen_color", color}});
      }
      for (const ImagePackedFile &part : image->packedfiles) {
        require(part.view == 0 && part.packedfile && part.packedfile->data &&
                    part.packedfile->size > 0 && packed.insert(part.tile_number).second,
                "Raw UDIM stock composite packed descriptor changed");
        packed_rows.append(QJsonObject{
            {"tile", part.tile_number}, {"view", part.view}, {"bytes", part.packedfile->size}});
      }
      const bool generated = packed.empty();
      composite_rows.append(QJsonObject{{"role", role},
                                        {"node", texture->name},
                                        {"image_uid", qint64(image->id.session_uid)},
                                        {"source", image->source},
                                        {"alpha", image->alpha_mode},
                                        {"space", image->colorspace_settings.name},
                                        {"generated_uniform", generated},
                                        {"tiles", tile_rows},
                                        {"packed", packed_rows}});
      udim_stock_["composites"] = composite_rows;
      write(false);
      require(declared == saved_set.tiles &&
                  (generated ? generated_descriptors : packed == saved_set.tiles),
              "Raw UDIM stock composite lacks exact full generated or packed tiles");
      for (int number : saved_set.tiles) {
        ImageUser user{};
        user.tile = number;
        void *lock = nullptr;
        auto *buffer = BKE_image_acquire_ibuf(image, &user, &lock);
        const auto release = qScopeGuard([&] {
          if (buffer)
            BKE_image_release_ibuf(image, buffer, lock);
        });
        require(buffer && buffer->x == saved_set.width(number) &&
                    buffer->y == saved_set.rows(number) && buffer->channels == 4 &&
                    buffer->float_data(),
                "Raw UDIM stock composite decoded tile descriptor changed");
      }
      audit.audited.insert(image);
    }
    double maximum = 0, runtime_delta = 0;
    int components = 0;
    QJsonArray rows;
    const auto retirement = qScopeGuard([&] {
      hooks_.host.with_gpu([&] {
        for (Image &image : main.images)
          BKE_image_free_gputextures(&image);
      });
    });
    hooks_.host.with_gpu([&] {
      for (const auto output : {pd149_sources_test::Output::Height,
                                pd149_sources_test::Output::Metallic,
                                pd149_sources_test::Output::Roughness,
                                pd149_sources_test::Output::AO})
      {
        pd149_sources_test::SourceGpuTap tap(*owner, output, 128, 128, audit);
        const auto references = output == pd149_sources_test::Output::Height ?
                                    captures_.last().toObject()["probes"].toArray() :
                                    udim_scalars_.last().toObject()["probes"].toArray();
        require(references.size() == 32, "Raw UDIM stock expected rows missing");
        int index = 0;
        for (int tile : {1001, 1002}) {
          const auto values = tap.sample(tile);
          require(values.size() == 128 * 128, "Raw UDIM stock packet shape");
          for (int y : {19, 51, 83, 115})
            for (int x : {13, 45, 77, 109}) {
              const auto row = references[index++].toObject();
              require(row["tile"].toInt() == tile && row["xy"].toArray() == QJsonArray{x, y},
                      "Raw UDIM stock expected position mismatch");
              const int c = output == pd149_sources_test::Output::Height    ? 0 :
                            output == pd149_sources_test::Output::Metallic  ? 0 :
                            output == pd149_sources_test::Output::Roughness ? 1 :
                                                                              2;
              const auto independent = row["independent"], runtime = row["actual"];
              require(runtime.isArray() &&
                          runtime.toArray().size() ==
                              (output == pd149_sources_test::Output::Height ? 1 : 3) &&
                          ((independent.isArray() && independent.toArray().size() == 3) ||
                           independent.isDouble()),
                      "Raw UDIM stock reference shape/type");
              const double want = independent.isArray() ? independent.toArray()[c].toDouble() :
                                                          independent.toDouble();
              const double observed = values[y * 128 + x][0];
              require(std::isfinite(observed) && std::isfinite(want), "Raw UDIM stock nonfinite");
              maximum = std::max(maximum, std::abs(observed - want));
              const auto prior = runtime.toArray()[c];
              require(prior.isDouble() && std::isfinite(prior.toDouble()),
                      "Raw UDIM runtime reference nonfinite");
              runtime_delta = std::max(runtime_delta, std::abs(observed - prior.toDouble()));
              ++components;
              rows.append(QJsonObject{{"tile", tile},
                                      {"xy", QJsonArray{x, y}},
                                      {"output", int(output)},
                                      {"actual", observed},
                                      {"independent", want}});
            }
        }
      }
    });
    udim_stock_ = {{"observed", true},
                   {"components", components},
                   {"maximum_error", maximum},
                   {"runtime_delta", runtime_delta},
                   {"original_payloads", payloads},
                   {"runtime_aliases", aliases},
                   {"native_restore_called", false},
                   {"composites", composite_rows},
                   {"probes", rows}};
    write(false);
    require(G_MAIN == live && components == 128 && maximum < 3e-5 && runtime_delta < 3e-5,
            "Raw UDIM stock oracle exceeds unchanged3e-5");
  }
  void write(bool pass)
  {
    QFile file(output_ + "/pd149-bump-artist.json");
    require(file.open(QIODevice::WriteOnly), "Bump proof open failed");
    const QJsonObject proof{
        {"native_run_pass", pass},
        {"cold", cold_},
        {"scope",
         "raw grayscale Bump File/Shelf; actual SceneOnly fragment normal; no Normal UI/export, no final shaded lighting or performance claim"},
        {"raw_udim", udim_},
        {"udim_scalars", udim_scalars_},
        {"udim_stock", udim_stock_},
        {"source_aliases_before_srt", aliases_},
        {"height_reference",
         "original affine texels, observed RTX q8 fractions, constant owned opacity; 3e-5 unchanged"},
        {"fragment_rgb8_bound", rgb8_bound},
        {"captures", captures_},
        {"fragments", fragments_},
        {"state", hooks_.state()}};
    require(file.write(QJsonDocument(proof).toJson()) >= 0, "Bump proof write failed");
  }
  void advance()
  {
    if (closed_ || busy_)
      return;
    QScopedValueRollback guard(busy_, true);
    require(++ticks_ < 18000 && clock_.elapsed() < 600000, "Bump actor bounded timeout");
    const auto state = hooks_.state();
    if (stage_ != logged_) {
      qInfo("PD149_BUMP_STAGE stage=%d", stage_);
      logged_ = stage_;
    }
    require(state["shader_failures"].toString().isEmpty(), "Bump real shader failed");
    if (stage_ > 1 && (state["composite_pending"].toBool() || state["property_pending"].toBool() ||
                       state["render_frames"].toInt() < frame_))
    {
      viewport_->update();
      return;
    }
    switch (stage_) {
      case 0:
        if (cold_) {
          require(!QFileInfo(source_).exists() && QFileInfo(project_).isFile(),
                  "Bump cold requires source-hidden packed project");
          hooks_.open(project_);
          stage_ = 100;
          break;
        }
        require(!QFileInfo(source_).exists() &&
                    QFileInfo(source_).absolutePath() == QFileInfo(mesh_).absolutePath(),
                "Bump fixture must create a new original beside its owned mesh");
        source_file(source_, udim_ ? 1001 : 0);
        if (udim_)
          source_file(QString(source_).replace(".1001.", ".1002."), 1002);
        action("newProject");
        stage_ = 1;
        break;
      case 1: {
        auto &d = get<QDialog>("newProjectDialog");
        d.findChild<QComboBox *>("projectTemplate")->setCurrentIndex(0);
        d.findChild<QLineEdit *>("meshPath")->setText(mesh_);
        d.findChild<QCheckBox *>("preserveUvTiles")->setChecked(true);
        auto *r = d.findChild<QComboBox *>("documentResolution");
        r->setCurrentIndex(r->findData(128));
        click("createProject");
        stage_ = 2;
        break;
      }
      case 2:
        if (!hooks_.document())
          return;
        hooks_.uv_view();
        edit();
        get<QCheckBox>("uniformMaterialLayeringEnabled").setChecked(true);
        click("uniformMaterialLayeringApply");
        stage_ = 3;
        break;
      case 3:
        if (window_->findChild<QDialog *>("uniformMaterialLayeringDialog"))
          return;
        neutral_ = metadata();
        height("neutral");
        begin_witness("neutral", 4);
        return;
      case 4:
        before_ = metadata();
        catalog_ = QJsonObject{{"resources", state["project_resources"]}};
        edit();
        choose(0);
        file();
        stage_ = 5;
        break;
      case 5:
        if (!loaded())
          return;
        click("uniformMaterialLayeringCancel");
        stage_ = 6;
        break;
      case 6:
        require(metadata() == before_ && state["project_resources"] == catalog_["resources"],
                "Bump Cancel mutated document/catalog");
        history_ = state["undo_count"].toInt();
        edit();
        choose(0);
        file();
        stage_ = 7;
        break;
      case 7:
        if (!loaded())
          return;
        click("uniformMaterialLayeringApply");
        stage_ = 8;
        break;
      case 8:
        if (window_->findChild<QDialog *>("uniformMaterialLayeringDialog"))
          return;
        require(state["undo_count"].toInt() == history_ + 1,
                "Bump Apply must create exactly one Undo");
        {
          const auto source = set().material_layering.materials[0].textures[4];
          require(bool(source), "Bump File Apply lost the source");
          const auto resources = state["project_resources"].toArray();
          require(resources.size() == 1 &&
                      QUuid(resources[0].toObject()["id"].toString()) ==
                          QUuid(QString::fromStdString(source->asset)) &&
                      resources[0].toObject()["hash"].toString() ==
                          QString::fromStdString(source->hash),
                  "Bump File Apply did not publish exactly its validated original resource");
        }
        mapped_ = metadata();
        height("file");
        begin_witness("file", 9);
        return;
      case 9:
        action("undoDocument");
        stage_ = 10;
        break;
      case 10:
        require(metadata() == neutral_, "Bump Undo did not restore neutral state");
        height("undo");
        begin_witness("undo-neutral", 11);
        return;
      case 11:
        action("redoDocument");
        stage_ = 12;
        break;
      case 12:
        require(metadata() == mapped_, "Bump Redo descriptor differs");
        height("redo");
        begin_witness("redo", 13);
        return;
      case 13:
        edit();
        choose(1);
        shelf();
        stage_ = 14;
        break;
      case 14:
        if (!loaded())
          return;
        if (udim_ && udim_role_ < 3) {
          auto &channels = get<QComboBox>("materialInputSourceChannel");
          channels.setCurrentIndex(channels.findData(std::array<int, 3>{1, 2, 5}[udim_role_++]));
          shelf();
          return;
        }
        click("uniformMaterialLayeringApply");
        stage_ = 15;
        break;
      case 15:
        if (window_->findChild<QDialog *>("uniformMaterialLayeringDialog"))
          return;
        get<QComboBox>("materialOpacitySubstack").setCurrentIndex(1);
        click("addFillLayer");
        stage_ = 16;
        break;
      case 16: {
        const int slot = set().material_layering.mask_slots[0];
        auto *slider = window_->findChild<QWidget *>(QString("fillGradientUser%1").arg(slot));
        require(slider, "Bump real opacity Fill control missing");
        visible(*slider);
        const QPointF p(slider->width() - 2, slider->height() / 2.);
        for (auto kind : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
          QMouseEvent e(kind,
                        p,
                        slider->mapToGlobal(p.toPoint()),
                        Qt::LeftButton,
                        kind == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                        Qt::NoModifier);
          QApplication::sendEvent(slider, &e);
        }
        input_ = 1;
        stage_ = 17;
        break;
      }
      case 17:
        height("shelf");
        aliases_ = aliases();
        get<QComboBox>("materialOpacitySubstack").setCurrentIndex(0);
        edit();
        choose(1);
        number(get<QDoubleSpinBox>("materialInputMapping0"), udim_ ? .875 : 2);
        number(get<QDoubleSpinBox>("materialInputMapping2"), .125);
        click("uniformMaterialLayeringApply");
        stage_ = 18;
        break;
      case 18:
        if (window_->findChild<QDialog *>("uniformMaterialLayeringDialog"))
          return;
        require(aliases() == aliases_, "Bump SRT replaced source Image/GPU allocations");
        if (udim_) {
          std::set<QString> image_ids, gpu_ids;
          for (const auto &value : aliases_) {
            const auto pair = value.toArray();
            require(pair.size() == 2 && pair[0].isString() && pair[1].isString() &&
                        !pair[0].toString().isEmpty() && !pair[1].toString().isEmpty() &&
                        pair[0].toString() != "0" && pair[1].toString() != "0",
                    "Raw UDIM source alias/handle missing");
            image_ids.insert(pair[0].toString());
            gpu_ids.insert(pair[1].toString());
          }
          require(image_ids.size() == 2 && gpu_ids.size() == 2,
                  "Raw UDIM members were duplicated across roles/inputs");
        }
        srt_ = metadata();
        height("srt");
        begin_witness("srt", 19);
        return;
      case 19:
        edit();
        for (int input : {0, 1}) {
          choose(input);
          click("materialInputClear");
        }
        click("uniformMaterialLayeringApply");
        stage_ = 20;
        break;
      case 20:
        if (window_->findChild<QDialog *>("uniformMaterialLayeringDialog"))
          return;
        height("cleared");
        begin_witness("cleared-neutral", 21);
        return;
      case 21:
        action("undoDocument");
        stage_ = 22;
        break;
      case 22:
        require(metadata() == srt_, "Bump Clear Undo lost maps/SRT");
        height("restored");
        begin_witness("restored", 23);
        return;
      case 23:
        before_ = metadata();
        history_ = state["undo_count"].toInt();
        edit();
        choose(1);
        number(get<QDoubleSpinBox>("materialInputMapping2"), .25);
        number(get<QDoubleSpinBox>("materialInputMapping2"), .125);
        click("uniformMaterialLayeringApply");
        stage_ = 24;
        break;
      case 24:
        if (window_->findChild<QDialog *>("uniformMaterialLayeringDialog"))
          return;
        require(metadata() == before_ && state["undo_count"].toInt() == history_,
                "Bump unchanged Apply created history or mutated metadata");
        require(!emission_ && !restore_view_, "Bump Surface/layout not restored before Save");
        hooks_.save(project_);
        stage_ = 25;
        break;
      case 25:
        if (state["save_pending"].toBool() || !QFileInfo(project_).isFile())
          return;
        require(state["save_error"].toString().isEmpty(),
                qPrintable(state["save_error"].toString()));
        height("saved");
        if (udim_)
          capture_udim_stock();
        {
          QFile f(output_ + "/expected-materials.json");
          require(
              f.open(QIODevice::WriteOnly) &&
                  f.write(
                      QJsonDocument(material_layering_json(set().material_layering)).toJson()) > 0,
              "Bump expected metadata write");
        }
        finish();
        return;
      case 90:
        fragment();
        return;
      case 100:
        if (state["open_pending"].toBool())
          return;
        require(state["open_error"].toString().isEmpty(),
                qPrintable(state["open_error"].toString()));
        require(hooks_.document(), "Bump cold Open did not publish document");
        hooks_.uv_view();
        input_ = 1;
        stage_ = 101;
        break;
      case 101: {
        QFile f(qEnvironmentVariable("BTS_PD149_MAP_EXPECTED"));
        require(f.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(f.readAll()).object() ==
                                                   material_layering_json(set().material_layering),
                "Bump cold descriptors differ");
        height("source-hidden-cold");
        begin_witness("source-hidden-cold", 102);
        return;
      }
      case 102:
        if (udim_) {
          project_ = output_ + "/udim-resaved.blend";
          hooks_.save(project_);
          stage_ = 103;
          break;
        }
        finish();
        return;
      case 103:
        if (state["save_pending"].toBool() || !QFileInfo(project_).isFile())
          return;
        require(state["save_error"].toString().isEmpty(),
                qPrintable(state["save_error"].toString()));
        capture_udim_stock();
        finish();
        return;
    }
    frame_ = hooks_.state()["render_frames"].toInt() + 3;
    viewport_->update();
  }
  void fail(const char *error)
  {
    try {
      restore_witness();
      write(false);
    }
    catch (...) {
    }
    retire();
    qWarning("PD149_BUMP_UI FAILED %s", error);
    QCoreApplication::exit(1);
  }
  void finish()
  {
    require(!emission_ && !restore_view_, "Bump witness escaped final restoration");
    write(true);
    retire();
    std::puts("PD149_BUMP_UI PASS");
    QCoreApplication::exit(0);
  }

 public:
  BumpArtist(
      QWidget *w, QWidget *v, QString output, QString mesh, MaterialLayeringArtistHooks hooks)
      : QObject(w),
        window_(w),
        viewport_(v),
        output_(std::move(output)),
        mesh_(std::move(mesh)),
        source_(qEnvironmentVariable("BTS_PD149_MAP_SOURCE")),
        project_(qEnvironmentVariable("BTS_PD149_MAP_PROJECT")),
        hooks_(std::move(hooks)),
        cold_(qEnvironmentVariableIsSet("BTS_PD149_MAP_COLD"))
  {
    clock_.start();
    connect(&timer_, &QTimer::timeout, this, [this] {
      try {
        advance();
      }
      catch (const std::exception &e) {
        fail(e.what());
      }
    });
    timer_.start(33);
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { retire(); });
  }
  void retire() noexcept
  {
    if (closed_)
      return;
    closed_ = true;
    timer_.stop();
    try {
      restore_witness();
    }
    catch (...) {
      qFatal("Bump fragment witness did not retire before Host shutdown");
    }
  }
  ~BumpArtist()
  {
    if (emission_ || restore_view_)
      qFatal("Bump witness escaped owner cleanup");
  }
};
