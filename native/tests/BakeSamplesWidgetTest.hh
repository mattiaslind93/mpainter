#pragma once
#include "BakeQuality.hh"
#include "SkewPanel.hh"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <stdexcept>
namespace bts::bl_widget_test {
inline void require(bool ok, const char *message)
{
  if (!ok)
    throw std::runtime_error(message);
}
inline void key(QWidget &widget,
                int code,
                Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                const QString &text = {})
{
  QKeyEvent down(QEvent::KeyPress, code, modifiers, text),
      up(QEvent::KeyRelease, code, modifiers, text);
  QApplication::sendEvent(&widget, &down);
  QApplication::sendEvent(&widget, &up);
}
inline void reveal(QWidget &widget)
{
  for (auto *p = widget.parentWidget(); p; p = p->parentWidget())
    if (auto *scroll = qobject_cast<QScrollArea *>(p))
      scroll->ensureWidgetVisible(&widget);
  widget.setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  require(widget.isVisible() && widget.isEnabled(), "BL control is not visible and enabled");
}
inline void select_index(QComboBox &widget, int target)
{
  reveal(widget);
  require(target >= 0 && target < widget.count(), "BL choice is absent");
  key(widget, Qt::Key_Home);
  for (int i = 0; i < target; ++i)
    key(widget, Qt::Key_Down);
  require(widget.currentIndex() == target, "BL keyboard combo selection failed");
}
inline void select(QComboBox &widget, int value)
{
  select_index(widget, widget.findData(value));
  require(widget.currentData().toInt() == value, "BL combo value changed");
}
inline void type(QSpinBox &widget, int value)
{
  reveal(widget);
  key(widget, Qt::Key_A, Qt::ControlModifier);
  for (const auto c : QString::number(value))
    key(widget, Qt::Key_0 + c.digitValue(), Qt::NoModifier, QString(c));
  key(widget, Qt::Key_Tab);
  QApplication::processEvents();
  require(widget.value() == value, "BL typed sample value changed");
}
inline void click(QWidget &widget)
{
  reveal(widget);
  const QPointF point = widget.rect().center(), global = widget.mapToGlobal(point.toPoint());
  QMouseEvent down(
      QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier),
      up(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(&widget, &down);
  QApplication::sendEvent(&widget, &up);
}
template<class T> inline T &control(QWidget &parent, const char *name)
{
  auto *p = parent.findChild<T *>(name);
  require(p, "Missing real BL widget");
  return *p;
}
inline QJsonObject skew(const QString &output)
{
  QDialog window;
  auto *layout = new QVBoxLayout(&window);
  auto *panel = new SkewPanel(&window);
  layout->addWidget(panel);
  const auto set = QUuid::createUuid(), other = QUuid::createUuid();
  SkewSettings settings;
  QJsonObject recipe{{"project_mesh", true},
                     {"resolution", 128},
                     {"height", 128},
                     {"rays", 64},
                     {"sampling", 1},
                     {"dilation", 11},
                     {"enabled", QJsonArray{true, true, true, true, true, true, true}}};
  for (const auto &id : {set, other})
    for (int role = 0; role < 7; ++role)
      recipe = with_bake_quality(recipe,
                                 id,
                                 role,
                                 {{"rays", 32 + role},
                                  {"sampling", 2},
                                  {"dilation", role + 3},
                                  {"diffusion", role % 2 == 0}});
  int recipe_changes = 0, quality_changes = 0;
  const auto sync = [&] {
    panel->synchronize(recipe, settings, false, false, set, -1, -1, 2, "BL real widgets");
  };
  panel->recipe_changed = [&](const QJsonObject &r) {
    ++recipe_changes;
    recipe = r;
    sync();
  };
  panel->quality_changed = [&](int role, const QJsonObject &q) {
    ++quality_changes;
    recipe = with_bake_quality(recipe, set, role, q);
    sync();
  };
  sync();
  window.resize(1024, 768);
  window.show();
  QApplication::processEvents();
  auto &rays = control<QSpinBox>(*panel, "bakeCommonRays");
  auto &preset = control<QComboBox>(*panel, "bakeCommonRaysPreset");
  auto &aa = control<QComboBox>(*panel, "bakeCommonSampling");
  auto &role = control<QComboBox>(*panel, "bakeQualityRole");
  auto &map_rays = control<QSpinBox>(*panel, "bakeMapRays");
  require(rays.minimum() == 1 && rays.maximum() == 512 && map_rays.minimum() == 1 &&
              map_rays.maximum() == 512,
          "BL spin range is not1..512");
  const auto check = [&](int count, int sampling) {
    require(recipe["rays"].toInt() == count && recipe["sampling"].toInt() == sampling,
            "Common BL values not published");
    for (const auto &id : {set, other})
      for (int i = 0; i < 7; ++i) {
        const auto q = bake_quality(recipe, id, i);
        require(q["rays"].toInt() == count && q["sampling"].toInt() == sampling &&
                    q["dilation"].toInt() == i + 3 && q["diffusion"].toBool() == (i % 2 == 0),
                "BL common sync changed unrelated quality or missed a map/set");
      }
  };
  QJsonArray selected;
  for (int count : {16, 128, 512}) {
    select(preset, count);
    select(aa, 8);
    check(count, 8);
    selected.append(count);
  }
  for (int count : {17, 511}) {
    type(rays, count);
    require(preset.currentData().toInt() == -1, "Custom ray count was not retained as Custom");
    check(count, 8);
    selected.append(count);
  }
  for (int sampling : {1, 2, 4, 8}) {
    select(aa, sampling);
    check(511, sampling);
  }
  for (int i = 0; i < 7; ++i) {
    select_index(role, i);
    require(map_rays.isEnabled(), "Per-map ray control disabled for a non-ray map");
    type(map_rays, 17 + i);
    require(bake_quality(recipe, set, i)["rays"].toInt() == 17 + i,
            "Per-map keyboard edit was not published");
  }
  select(preset, 512);
  select(aa, 8);
  check(512, 8);
  // Put the common controls back in view before recording their real geometry.
  reveal(rays);
  auto &width = control<QComboBox>(*panel, "autoBakeWidth");
  const int width_y = width.mapTo(&window, QPoint()).y(),
            rays_y = rays.mapTo(&window, QPoint()).y(), aa_y = aa.mapTo(&window, QPoint()).y();
  require(width_y < aa_y && aa_y < rays_y, "Common sampling controls are not below Output size");
  require(window.rect().contains(rays.mapTo(&window, rays.rect().center())),
          "Common ray control is outside1024-wide viewport");
  QDir().mkpath(output);
  require(window.grab().save(output + "/bl-skew-controls.png"), "Cannot save BL widget capture");
  return {{"ui", "SkewPanel"},
          {"window_width", window.width()},
          {"selected_rays", selected},
          {"aa", QJsonArray{1, 2, 4, 8}},
          {"map_roles_enabled_and_edited", 7},
          {"sets_synchronized", 2},
          {"recipe_changes", recipe_changes},
          {"quality_changes", quality_changes},
          {"output_y", width_y},
          {"aa_y", aa_y},
          {"rays_y", rays_y},
          {"final_recipe", recipe},
          {"gpu_executed", false}};
}
}  // namespace bts::bl_widget_test
