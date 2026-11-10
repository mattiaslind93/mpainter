// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkewPanel.hh"
#include <QApplication>
#include <QJsonArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFormLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <cassert>
#include <cstdio>
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  app.setStyle("Fusion");
  QDialog window;
  window.setStyleSheet("QWidget{background:#303030;color:#d0d0d0;font-size:12px;}");
  auto *layout = new QVBoxLayout(&window);
  auto *panel = new bts::SkewPanel(&window);
  layout->addWidget(panel);
  QJsonArray enabled;
  for (int i = 0; i < 7; ++i)
    enabled.append(true);
  QJsonObject recipe{
      {"project_mesh", true}, {"resolution", 128}, {"height", 128}, {"enabled", enabled}};
  const auto id = QUuid::createUuid();
  bts::SkewSettings skew;
  panel->recipe_changed = [&](const auto &r) {
    recipe = r;
    panel->synchronize(recipe, skew, false, false, id, -1, -1, 1, "FixtureSurface");
  };
  panel->synchronize(recipe, skew, false, false, id, -1, -1, 1, "FixtureSurface");
  window.resize(960, 688);
  window.show();
  app.processEvents();
  auto *scroll = panel->findChild<QScrollArea *>("bakingSettingsScroll");
  assert(scroll && scroll->horizontalScrollBar()->maximum() == 0);
  auto *project = panel->findChild<QCheckBox *>("skewUseProjectMesh");
  assert(project && project->text() == "Use Low Poly Mesh as High Poly Mesh");
  assert(project->parentWidget()->objectName() == "bakingCommonParameters" &&
         !project->isHidden());
  const auto source_choice = [&](QJsonObject choice, bool expected) {
    panel->synchronize(choice, skew, false, false, id, -1, -1, 1, "FixtureSurface");
    assert(project->isChecked() == expected);
  };
  source_choice(QJsonObject{}, true);
  source_choice(QJsonObject{{"high_files", QJsonArray{"fixture-high.obj"}}}, false);
  source_choice(
      QJsonObject{{"project_mesh", true}, {"high_files", QJsonArray{"fixture-high.obj"}}}, true);
  source_choice(QJsonObject{{"project_mesh", false}}, false);
  panel->synchronize(recipe, skew, false, false, id, -1, -1, 1, "FixtureSurface");
  auto *bake = panel->findChild<QPushButton *>("bakeSelectedMaps");
  assert(bake->isEnabled());
  for (int role = 0; role < 7; ++role)
    panel->findChild<QCheckBox *>(QString("bakeSelectedMap%1").arg(role))->click();
  assert(!bake->isEnabled() && bake->toolTip().contains("at least one"));
  panel->findChild<QCheckBox *>("bakeSelectedMap5")->click();
  assert(bake->isEnabled());
  int requests = 0;
  panel->bake_selected = [&] { ++requests; };
  bake->click();
  assert(requests == 1);
  const auto selected = recipe["enabled"].toArray();
  assert(selected.size() == 7);
  for (int role = 0; role < 7; ++role)
    assert(selected[role].toBool() == (role == 5));
  auto *cage = panel->findChild<QToolButton *>("bakingCageSection");
  assert(!cage->isChecked());
  cage->click();
  assert(cage->isChecked());
  app.processEvents();
  assert(scroll->horizontalScrollBar()->maximum() == 0);
  cage->click();
  app.processEvents();
  assert(panel->findChild<QToolButton *>("bakingQualitySection")->y() - cage->geometry().bottom() <
         20);
  auto *common_rays = panel->findChild<QSpinBox *>("bakeCommonRays");
  auto *common_preset = panel->findChild<QComboBox *>("bakeCommonRaysPreset");
  auto *common_sampling = panel->findChild<QComboBox *>("bakeCommonSampling");
  auto *map_rays = panel->findChild<QSpinBox *>("bakeMapRays");
  assert(common_rays && common_preset && common_sampling && map_rays &&
         common_rays->parentWidget()->objectName() == "bakingCommonParameters" &&
         common_sampling->parentWidget()->objectName() == "bakingCommonParameters");
  assert(common_rays->minimum() == 1 && common_rays->maximum() == 512 &&
         map_rays->maximum() == 512 && map_rays->isEnabled());
  common_preset->setCurrentIndex(common_preset->findData(512));
  assert(common_rays->value() == 512 && recipe["rays"].toInt() == 512 && map_rays->value() == 512);
  common_sampling->setCurrentIndex(common_sampling->findData(4));
  assert(recipe["sampling"].toInt() == 4);
  for (auto *form : panel->findChildren<QFormLayout *>())
    for (int row = 0; row < form->rowCount(); ++row)
      if (auto *item = form->itemAt(row, QFormLayout::LabelRole); item && item->widget())
        assert(item->widget()->width() == 170);
  if (argc > 1) {
    QDir().mkpath(argv[1]);
    window.grab().save(QString(argv[1]) + "/baking-widget.png");
  }
  std::puts(
      "Baking UI: independent columns, common label widths, collapsible sections, selected maps and disabled reason PASS");
}
