// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic input and JSON state snapshots that main()'s demo and acceptance runs drive the app
// with. main.cc includes this inside its anonymous namespace; it is not a standalone header.
void send_mouse_event(QWidget &target,
                      QEvent::Type type,
                      QPointF point,
                      Qt::MouseButton button,
                      Qt::MouseButtons buttons,
                      Qt::KeyboardModifiers modifiers)
{
  QMouseEvent event(type, point, target.mapToGlobal(point), button, buttons, modifiers);
  QCoreApplication::sendEvent(&target, &event);
}

void send_drag(QWidget &target,
                QPointF start,
                QPointF delta,
                Qt::MouseButton button,
                Qt::KeyboardModifiers modifiers,
                std::optional<Qt::KeyboardModifiers> move_modifiers = std::nullopt)
{
  send_mouse_event(target, QEvent::MouseButtonPress, start, button, button, modifiers);
  for (int step = 1; step <= 6; ++step) {
    send_mouse_event(target, QEvent::MouseMove, start + delta * (double(step) / 6),
                      Qt::NoButton, button, move_modifiers.value_or(modifiers));
  }
  send_mouse_event(target, QEvent::MouseButtonRelease, start + delta,
                    button, Qt::NoButton, move_modifiers.value_or(modifiers));
}

void send_tablet_event(QWidget &target,QEvent::Type type,QPointF point,double pressure)
{
  static const QPointingDevice device("BTS synthetic pen test",0x425453,QInputDevice::DeviceType::Stylus,
      QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
  const bool released=type==QEvent::TabletRelease;
  QTabletEvent event(type,&device,point,target.mapToGlobal(point),pressure,0,0,0,0,0,Qt::NoModifier,
      type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,released?Qt::NoButton:Qt::LeftButton);
  QCoreApplication::sendEvent(&target,&event);
}

void send_key(QWidget &target, int key,Qt::KeyboardModifiers modifiers=Qt::NoModifier)
{
  // Qt may consume a shortcut without accepting the original key event.
  // Observe synchronous QAction delivery as well as the widget key handler.
  QObject shortcut_observer;
  bool shortcut_triggered = false;
  for (auto *action : target.window()->findChildren<QAction *>()) {
    QObject::connect(action, &QAction::triggered, &shortcut_observer,
                     [&] { shortcut_triggered = true; });
  }
  QKeyEvent press(QEvent::KeyPress, key, modifiers);
  press.setAccepted(false);
  QCoreApplication::sendEvent(&target, &press);
  if (!press.isAccepted() && !shortcut_triggered) {
    qFatal("BTS_DEMO_KEY_REJECTED key=%d", key);
  }
  QKeyEvent release(QEvent::KeyRelease, key, modifiers);
  QCoreApplication::sendEvent(&target, &release);
}

void send_wheel(QWidget &target, QPointF point, int delta)
{
  QWheelEvent event(point, target.mapToGlobal(point), {}, {0, delta}, Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, false);
  QCoreApplication::sendEvent(&target, &event);
}

QJsonObject native_document_state(bts::BlenderHost &host)
{
  const bts::SceneSnapshot snapshot = host.scene_snapshot();
  QJsonArray materials;
  for (const auto &material : snapshot.materials) {
    const auto &color = material.display_base_color;
    materials.append(QJsonObject{
        {"name", QString::fromStdString(material.name)},
        {"session_id", qint64(material.session_id)},
        {"roughness", material.roughness},
        {"constant_roughness", material.constant_roughness},
        {"linear_base_color", QJsonArray{material.values.base_color[0],material.values.base_color[1],material.values.base_color[2],material.values.base_color[3]}},
        {"metallic", material.values.metallic},
        {"display_base_color", QJsonArray{color[0], color[1], color[2], color[3]}},
        {"constant_base_color", material.constant_base_color}});
  }
  return {{"materials", materials}, {"mesh_objects", qint64(snapshot.mesh_objects)},
          {"uv_triangles", qint64(snapshot.uv_triangles.size())},
          {"bounds_minimum", QJsonArray{snapshot.minimum[0], snapshot.minimum[1], snapshot.minimum[2]}},
          {"bounds_maximum", QJsonArray{snapshot.maximum[0], snapshot.maximum[1], snapshot.maximum[2]}}};
}

QJsonObject workspace_state(const bts::WorkspaceShell &workspace)
{
  const QListWidget *sets = workspace.texture_sets();
  QJsonArray names;
  for (int row = 0; row < sets->count(); ++row) {
    names.append(sets->item(row)->text());
  }
  const QListWidgetItem *selected = sets->currentItem();
  QJsonArray layer_icons,context_options,brush_channels,layer_rows,selected_layers;
  const auto *context=workspace.findChild<QComboBox *>("layerChannelContext");
  for(int i=0;i<context->count();++i)context_options.append(context->itemText(i));
  for(int c=0;c<5;++c)brush_channels.append(workspace.findChild<QToolButton *>(QString("brushChannel%1").arg(c))->isChecked());
  for(int row=0;row<workspace.layer_stack()->count();++row) {
    const auto *item=workspace.layer_stack()->item(row);
    if(item->isSelected())selected_layers.append(item->data(Qt::UserRole).toString());
    layer_rows.append(QJsonObject{{"id",item->data(Qt::UserRole).toString()},{"depth",item->data(Qt::UserRole+2).toInt()},{"folder",item->data(Qt::UserRole+3).toBool()},{"collapsed",item->data(Qt::UserRole+4).toBool()}});
    const auto image=workspace.layer_stack()->item(row)->icon().pixmap(48,48).toImage().convertToFormat(QImage::Format_RGBA8888);
    layer_icons.append(QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex()));
  }
  return {{"brush_alignment",workspace.findChild<QComboBox *>("brushAlignment")->currentText()},
          {"backface_culling",workspace.findChild<QCheckBox *>("brushBackfaceCulling")->isChecked()},
          {"culling_control_enabled",workspace.findChild<QCheckBox *>("brushBackfaceCulling")->isEnabled()},
          {"culling_angle",workspace.findChild<QDoubleSpinBox *>("brushCullingAngle")->value()},
          {"culling_angle_enabled",workspace.findChild<QDoubleSpinBox *>("brushCullingAngle")->isEnabled()},
          {"alpha_name",workspace.findChild<QLabel *>("alphaResourceName")->text()},
          {"alpha_inverted",workspace.findChild<QCheckBox *>("alphaInvert")->isChecked()},
          {"alpha_uniform",workspace.findChild<QDoubleSpinBox *>("alphaUniform")->value()},
          {"alpha_uniform_visible",workspace.findChild<QDoubleSpinBox *>("alphaUniform")->isVisible()},
          {"alpha_hardness_visible",workspace.findChild<QDoubleSpinBox *>("alphaHardness")->isVisible()},
          {"alpha_invert_visible",workspace.findChild<QCheckBox *>("alphaInvert")->isVisible()},
          {"mask_grayscale",workspace.findChild<QDoubleSpinBox *>("maskGrayscale")->value()},{"mask_grayscale_visible",workspace.findChild<QDoubleSpinBox *>("maskGrayscale")->isVisible()},{"selected_layers",selected_layers},{"layer_rows",layer_rows},{"layer_context",context->currentText()},{"layer_context_options",context_options},
          {"layer_thumbnail_hashes",layer_icons},{"brush_channels",brush_channels},{"texture_sets", names},
          {"selected_texture_set", sets->currentRow()},
          {"selected_layer_id", workspace.layer_stack()->currentItem() ? workspace.layer_stack()->currentItem()->data(Qt::UserRole).toString() : QString{}},
          {"selected_material_name", selected ? selected->text() : QString{}},
          // Diagnostic identity for this process, never a persisted document identifier.
          {"selected_material_session_id", selected ? qint64(selected->data(Qt::UserRole).toUInt()) : 0},
          {"pressure_size",workspace.findChild<QToolButton *>("brushPressureSize")->isChecked()},
          {"pressure_flow",workspace.findChild<QToolButton *>("brushPressureFlow")->isChecked()},
          {"toolbar_pressure_size",workspace.findChild<QToolButton *>("toolbarPressureSize")->isChecked()},
          {"toolbar_pressure_flow",workspace.findChild<QToolButton *>("toolbarPressureFlow")->isChecked()},
          {"minimum_size",workspace.findChild<QDoubleSpinBox *>("brushMinimumSize")->value()},
          {"minimum_flow",workspace.findChild<QDoubleSpinBox *>("brushMinimumFlow")->value()},
          {"minimum_size_enabled",workspace.findChild<QDoubleSpinBox *>("brushMinimumSize")->isEnabled()},
          {"minimum_flow_enabled",workspace.findChild<QDoubleSpinBox *>("brushMinimumFlow")->isEnabled()},
          {"lazy_mouse",workspace.findChild<QToolButton *>("lazyMouseToggle")->isChecked()},{"lazy_distance",workspace.findChild<QDoubleSpinBox *>("lazyMouseDistance")->value()},{"lazy_distance_enabled",workspace.findChild<QDoubleSpinBox *>("lazyMouseDistance")->isEnabled()},
          {"brush_spacing",workspace.findChild<QDoubleSpinBox *>("brushSpacing")->value()},
          {"toolbar_brush_spacing",workspace.findChild<QDoubleSpinBox *>("toolbarBrushSpacing")->value()},
          {"eraser_active",workspace.findChild<QToolButton *>("eraserTool")->isChecked()},
          {"stroke_opacity",workspace.findChild<QDoubleSpinBox *>("brushStrokeOpacity")->value()},
          {"toolbar_stroke_opacity",workspace.findChild<QDoubleSpinBox *>("toolbarStrokeOpacity")->value()},
          {"brush_flow",workspace.findChild<QDoubleSpinBox *>("brushFlow")->value()},
          {"toolbar_brush_flow",workspace.findChild<QDoubleSpinBox *>("toolbarBrushFlow")->value()},
          {"brush_size",workspace.findChild<QDoubleSpinBox *>("brushSize")->value()},
          {"toolbar_brush_size",workspace.findChild<QDoubleSpinBox *>("toolbarBrushSize")->value()},
          {"undo_enabled",workspace.findChild<QAction *>("undoDocument") ? workspace.findChild<QAction *>("undoDocument")->isEnabled() : false},
          {"redo_enabled",workspace.findChild<QAction *>("redoDocument") ? workspace.findChild<QAction *>("redoDocument")->isEnabled() : false},
          {"roughness", workspace.roughness_control()->value()},
          {"roughness_visible", workspace.roughness_control()->isVisible()},
          {"metallic_visible", workspace.metallic_control()->isVisible()},
          {"roughness_editable", workspace.roughness_control()->isEnabled()},
          {"viewport_layout", workspace.view_layout_control()->currentText()},
          {"camera_projection", workspace.projection_control()->currentText()},
          {"display_transform", workspace.display_transform_control()->currentText()},
          {"settings_tab", workspace.layer_settings_tabs()->currentIndex()},
          {"console_visible", workspace.console_visible()}};
}
