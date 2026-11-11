// SPDX-License-Identifier: GPL-3.0-or-later
// State queries for the UI and diagnostics (tangent shader, render state, ...).
// A section of native/host/BlenderHost.cc, which includes it in place; not a standalone file.
QJsonObject BlenderHost::tangent_shader_state() const
{
  auto &s = *impl_;
  s.require_owner();
  QJsonObject result;
  for (Material &material : s.main->materials)
    if (material.nodetree) {
      auto *node = bke::node_find_node_by_name(*material.nodetree, "__BTS_PaintNormal");
      if (!node)
        continue;
      int success = 0, failed = 0;
      auto *evaluated = DEG_get_evaluated(s.depsgraph, &material);
      for (Material *owner : std::set<Material *>{&material, evaluated}) {
        if (!owner)
          continue;
        for (LinkData &link : owner->gpumaterial) {
          const auto status = GPU_material_status(static_cast<GPUMaterial *>(link.data));
          success += status == GPU_MAT_SUCCESS;
          failed += status == GPU_MAT_FAILED;
        }
      }
      const auto name = QString::fromUtf8(material.id.name + 2);
      const auto cached = s.last_tangent_shader_state[name].toObject();
      if (success + failed == 0 && cached["fragment"].isBool() &&
          cached["fragment"].toBool() == bool(node->custom1 & 1))
      {
        success = cached["success"].toInt();
        failed = cached["failed"].toInt();
      }
      result[name] = QJsonObject{
          {"fragment", bool(node->custom1 & 1)}, {"success", success}, {"failed", failed}};
    }
  return result;
}
LensProjection BlenderHost::lens_projection() const
{
  impl_->require_owner();
  return impl_->lens;
}
void BlenderHost::set_display_post_effects(const QJsonObject &json)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.drawing || s.phase == Impl::Phase::Closed)
    throw std::logic_error("Display effects require an idle viewport");
  set_display_post_settings(*s.main, *s.scene, *s.view, DisplayPostSettings::from_json(json));
  s.lens = display_post_settings(*s.scene).projection();
}
void BlenderHost::edit_display_post_effect(const QString &key, double value)
{
  auto &s = *impl_;
  s.require_owner();
  auto state = display_post_settings(*s.scene);
  state.edit(key, value);
  set_display_post_effects(state.to_json());
}
void BlenderHost::set_subsurface_preview(const QJsonObject &json)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.drawing || s.phase == Impl::Phase::Closed)
    throw std::logic_error("Subsurface preview requires an idle viewport");
  const auto value = SubsurfacePreview::from_json(json);
  bts::set_subsurface_preview(*s.main, *s.scene, value);
  s.inspection.set_subsurface_enabled(*s.main, value.enabled);
  s.tag_preview();
  s.texture_dirty = true;
}
void BlenderHost::edit_subsurface_preview(const QString &key, double value)
{
  impl_->require_owner();
  if (impl_->phase == Impl::Phase::Closed)
    throw std::logic_error("Subsurface preview is closed");
  auto next = subsurface_preview(*impl_->scene);
  if (!std::isfinite(value))
    throw std::invalid_argument("Invalid subsurface preview edit");
  if (key == "enabled")
    next.enabled = value != 0;
  else if (key == "samples") {
    if (value != std::floor(value))
      throw std::invalid_argument("Use an integer subsurface sample count");
    next.samples = int(value);
  }
  else if (key == "reset")
    next = {};
  else
    throw std::invalid_argument("Unknown subsurface preview property");
  set_subsurface_preview(next.to_json());
}
void BlenderHost::set_render_preview(bool enabled, bool force_cpu)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.drawing || s.phase != Impl::Phase::GpuReady)
    throw std::logic_error("Renderer switch requires an idle GPU viewport");
  s.enter_gpu();
  try {
    s.configure_preview(enabled, force_cpu);
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
void BlenderHost::set_render_quality(int samples, bool denoise)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.drawing || samples < 1 || samples > 65536)
    throw std::invalid_argument("Invalid Cycles sample count");
  auto root = RNA_id_pointer_create(&s.scene->id);
  auto settings = RNA_pointer_get(&root, "cycles");
  const bool decrease = samples < RNA_int_get(&settings, "preview_samples");
  RNA_int_set(&settings, "preview_samples", samples);
  RNA_boolean_set(&settings, "use_preview_denoising", denoise);
  RNA_property_update(
      s.context, &settings, RNA_struct_find_property(&settings, "preview_samples"));
  RNA_property_update(
      s.context, &settings, RNA_struct_find_property(&settings, "use_preview_denoising"));
  DEG_id_tag_update_ex(s.main, &s.scene->id, ID_RECALC_PARAMETERS | ID_RECALC_SYNC_TO_EVAL);
  if (decrease &&
      STREQ(s.scene->r.engine, "CYCLES")) { /* Cycles only increases a running viewport sample
                                               budget. Restart for a lower limit. */
    s.enter_gpu();
    s.free_targets();
    DRW_gpu_context_disable_ex(false);
  }
}
void BlenderHost::diagnostic_cycles_cuda_render(const QString &directory)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase != Impl::Phase::SceneReady)
    throw std::logic_error(
        "CUDA diagnostic requires factory startup before viewport initialization");
  // Python snapshots os.environ at initialization; pass the path directly as
  // a JSON-escaped string literal instead of changing the process environment.
  QByteArray code = "output_directory = ";
  code += QJsonDocument(QJsonArray{QFileInfo(directory).absoluteFilePath()})
              .toJson(QJsonDocument::Compact);
  code += "[0]\n";
  code += cycles_cuda_test_python;
  if (!BPY_run_string_exec(s.context, nullptr, code.constData()))
    throw std::runtime_error("Cycles CUDA test failed; see result.json and the CUDA error above");
}
QJsonObject BlenderHost::render_state() const
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase == Impl::Phase::Closed)
    return {{"active", false}, {"available", false}, {"status", "Closed"}};
  const bool active = STREQ(s.scene->r.engine, "CYCLES");
  RenderEngine *engine = nullptr;
  if (s.region && s.region->regiondata) {
    auto *rv = static_cast<RegionView3D *>(s.region->regiondata);
    if (rv->view_render)
      engine = RE_view_engine_get(rv->view_render);
  }
  QString error;
  if (active && engine) {
    if (engine->re) {
      const auto *result = RE_AcquireResultRead(engine->re);
      if (result && result->error)
        error = QString::fromUtf8(result->error);
      RE_ReleaseResult(engine->re);
    }
    const auto text = QString::fromUtf8(engine->text);
    if (error.isEmpty() && text.startsWith("Cycles error: "))
      error = text.mid(14);
  }
  auto root = RNA_id_pointer_create(&s.scene->id);
  auto settings = RNA_pointer_get(&root, "cycles");
  const auto mirror_error = active ? s.inspection.cycles_images_error() : std::string_view{};
  const bool mirror_paused = !mirror_error.empty();
  QString message = engine ? QString::fromUtf8(engine->text) :
                    active ? QString("Initializing Cycles…") :
                             QString("EEVEE");
  if (active && s.inspection.cycles_images_pending())
    message = "Preparing live material images…";
  if (mirror_paused)
    message =
        QString(
            "Live image error: %1. Switch to Painting, free resources if needed, then return to Rendering to retry.")
            .arg(QString::fromUtf8(mirror_error.data(), mirror_error.size()));
  if (!error.isEmpty())
    message = "Cycles / " + s.preview_device + " error: " + error +
              ". Select CPU to retry, or F9 to return to painting.";
  return {{"cpu_fallback", active && s.preview_cpu_fallback},
          {"device_message", active ? s.preview_device_message : QString{}},
          {"error", error},
          {"image_mirror_paused", mirror_paused},
          {"active", active},
          {"view_clip_start", double(s.view->clip_start)},
          {"view_clip_end", double(s.view->clip_end)},
          {"available", RE_engines_find("CYCLES") != nullptr},
          {"device", active ? s.preview_device : QString("EEVEE")},
          {"status", message},
          {"samples", RNA_int_get(&settings, "preview_samples")},
          {"denoise", RNA_boolean_get(&settings, "use_preview_denoising")}};
}

QJsonObject BlenderHost::environment_state() const
{
  auto &s = *impl_;
  s.require_owner();
  return {
      {"subsurface", subsurface_preview(*s.scene).to_json()},
      {"post_depth_enabled", bool(s.view_layer->passflag & SCE_PASS_DEPTH)},
      {"post_effects", display_post_settings(*s.scene).to_json()},
      {"resource", s.view->shading.lookdev_light},
      {"rotation", double(s.view->shading.studiolight_rot_z) * 180 / 3.14159265358979323846},
      {"exposure", std::log2(std::max(double(s.view->shading.studiolight_intensity), 1. / 1024))},
      {"opacity", s.view->shading.studiolight_background * 100},
      {"blur", s.view->shading.studiolight_blur * 100},
      {"alignment",
       (s.view->shading.flag & V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION) ? "Local" : "World"},
      {"lighting_follows_view",
       bool(s.view->shading.flag & V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION)}};
}
void BlenderHost::set_environment_resource(const std::string &path)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.drawing || s.phase != Impl::Phase::GpuReady)
    throw std::logic_error("Environment change requires an idle viewport");
  auto *light = register_environment_file(QString::fromStdString(path));
  STRNCPY(s.view->shading.lookdev_light, light->name);
  s.tag_preview();
}
void BlenderHost::set_environment_alignment(bool world)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.drawing || s.phase != Impl::Phase::GpuReady)
    throw std::logic_error("Environment change requires an idle viewport");
  if (world)
    s.view->shading.flag &= ~V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION;
  else
    s.view->shading.flag |= V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION;
  s.tag_preview();
}
void BlenderHost::set_environment_rotation(double degrees)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.drawing || s.phase != Impl::Phase::GpuReady)
    throw std::logic_error("Environment change requires an idle viewport");
  if (!std::isfinite(degrees) || degrees < -180 || degrees > 180)
    throw std::invalid_argument("Environment rotation outside -180..180 degrees");
  s.view->shading.studiolight_rot_z = float(degrees * 3.14159265358979323846 / 180);
  s.tag_preview();
}

void BlenderHost::set_environment_appearance(const std::string &field, double value)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.drawing || s.phase != Impl::Phase::GpuReady)
    throw std::logic_error("Environment change requires an idle viewport");
  if (!std::isfinite(value))
    throw std::invalid_argument("Nonfinite environment value");
  if (field == "exposure" && value >= -10 && value <= 10)
    s.view->shading.studiolight_intensity = float(std::exp2(value));
  else if (field == "opacity" && value >= 0 && value <= 100)
    s.view->shading.studiolight_background = 0;  // Camera background is always the UI theme.
  else if (field == "blur" && value >= 0 && value <= 100)
    s.view->shading.studiolight_blur = float(value / 100);
  else
    throw std::invalid_argument("Unsupported environment control or value");
  s.tag_preview();
}

void BlenderHost::set_display_transform(const std::string &name)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.drawing || s.phase == Impl::Phase::Closed)
    throw std::logic_error("Display change outside idle session");
  auto scene = RNA_id_pointer_create(&s.scene->id);
  auto view = RNA_pointer_get(&scene, "view_settings");
  auto *property = RNA_struct_find_property(&view, "view_transform");
  int value;
  if (!RNA_property_enum_value(s.context, &view, property, name.c_str(), &value))
    throw std::invalid_argument("The native OCIO config does not offer this display transform");
  RNA_property_enum_set(&view, property, value);
  if (STREQ(s.scene->r.engine, "CYCLES"))
    s.sync_preview_background();
  DEG_id_tag_update_ex(s.main, &s.scene->id, ID_RECALC_SYNC_TO_EVAL);
  s.uv_dirty = true;
}

void BlenderHost::display_rgba(std::span<float> rgba) const
{
  auto &s = *impl_;
  s.require_owner();
  if (!s.scene || rgba.size() % 4)
    throw std::invalid_argument("Invalid display preview buffer");
  if (rgba.size() / 4 > std::uint64_t(std::numeric_limits<int>::max()))
    throw std::length_error("Native display processor pixel indexing exceeds signed int");
  auto processor = ColormanageProcessor::display_processor_new(&s.scene->view_settings,
                                                               &s.scene->display_settings);
  processor.apply(rgba.data(), int(rgba.size() / 4), 1, 4, false);
}

QJsonObject BlenderHost::color_state() const
{
  auto &s = *impl_;
  s.require_owner();
  QJsonArray matrix;
  for (int row = 0; row < 3; ++row) {
    QJsonArray values;
    for (int column = 0; column < 3; ++column)
      values.append(s.main->colorspace.scene_linear_to_xyz[column][row]);
    matrix.append(values);
  }
  return {{"working_space", s.main->colorspace.scene_linear_name},
          {"active_working_space", IMB_colormanagement_working_space_get()},
          {"scene_linear_to_xyz", matrix},
          {"native_acescg",
           math::is_equal(s.main->colorspace.scene_linear_to_xyz, ocio::ACESCG_TO_XYZ, 1e-5f) &&
               math::is_equal(colorspace::scene_linear_to_xyz, ocio::ACESCG_TO_XYZ, 1e-5f)},
          {"display", s.scene->display_settings.display_device},
          {"view_transform", s.scene->view_settings.view_transform},
          {"look", s.scene->view_settings.look},
          {"exposure", s.scene->view_settings.exposure},
          {"gamma", s.scene->view_settings.gamma}};
}

BlenderHost::~BlenderHost()
{
  shutdown();
}

void BlenderHost::release_bake_scratch()
{
  with_gpu([&] {
    GPU_finish();
    int before_total = 0, before_free = 0;
    if (GPU_mem_stats_supported())
      GPU_mem_stats_get(&before_total, &before_free);
    const auto reclaimed = impl_->inspection.release_bake_scratch();
    GPU_finish();
    int total = 0, free = 0;
    if (GPU_mem_stats_supported())
      GPU_mem_stats_get(&total, &free);
    texture_vram_query_valid = total > 0 && free >= 0 && free <= total;
    texture_free_vram = texture_vram_query_valid ? std::uint64_t(free) * 1024 : 0;
    ++texture_vram_query_generation;
    std::printf(
        "BTS_BAKE_VRAM total_kib=%d free_before_kib=%d free_after_kib=%d reclaimed_cache_bytes=%llu\n",
        total,
        before_free,
        free,
        (unsigned long long)reclaimed);
  });
}
void BlenderHost::with_gpu(const std::function<void()> &fn)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase != Impl::Phase::GpuReady || s.drawing)
    throw std::logic_error("GPU bake rays require an idle viewport");
  s.enter_gpu();
  try {
    fn();
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
void BlenderHost::initialize_gpu_metal()
{
#ifdef __APPLE__
  impl_->require_owner();
  if (impl_->phase != Impl::Phase::SceneReady)
    throw std::logic_error("Metal initialization requires scene-ready host");
  impl_->ghost = std::make_unique<MetalContextOwner>();
  initialize_gpu_surface();
#else
  throw std::logic_error("Metal viewport is available only on macOS");
#endif
}
QImage BlenderHost::viewport_image() const
{
  impl_->require_owner();
#ifdef __APPLE__
  return impl_->metal_frame;
#else
  return {};
#endif
}
void BlenderHost::initialize_gpu(QOpenGLWidget &surface)
{
  impl_->surface = std::make_unique<QtGpuSurface>(surface);
  initialize_gpu_surface();
}
void BlenderHost::initialize_gpu(QOpenGLContext &context, QSurface &surface, unsigned framebuffer)
{
  impl_->surface = std::make_unique<QtGpuSurface>(context, surface, framebuffer);
  initialize_gpu_surface();
}
void BlenderHost::initialize_gpu(QOpenGLContext &context,
                                 QSurface &surface,
                                 std::function<unsigned()> framebuffer)
{
  impl_->require_owner();
  if (impl_->phase != Impl::Phase::SceneReady)
    throw std::logic_error("GPU initialization requires scene-ready host");
  impl_->surface = std::make_unique<QtGpuSurface>(context, surface, std::move(framebuffer));
  initialize_gpu_surface();
}
