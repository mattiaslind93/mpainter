// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpsmBitmapOptions.hh"
#include "SpsmAoAliasOption.hh"
#include <QApplication>
#include <QVBoxLayout>
#include <QScrollArea>
#include <cassert>
#include <iostream>

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  QWidget preview;
  auto *layout = new QVBoxLayout(&preview);
  auto *scroll = new QScrollArea(&preview);
  scroll->setWidgetResizable(true);
  auto *body = new QWidget;
  auto *form = new QVBoxLayout(body);
  scroll->setWidget(body);
  layout->addWidget(scroll);
  bts::SpsmAoAliasOption maps(body);
  form->addWidget(&maps);
  assert(maps.isChecked() && !maps.policy_tags().empty());
  maps.setChecked(false);
  assert(maps.policy_tags().empty());
  maps.setChecked(true);
  assert(maps.policy_tags() ==
         QStringList{"spsm:policy:meshmap-ao-color=native-ao-generator-uv-identity-nearest"});
  maps.setChecked(false);
  assert(maps.policy_tags().empty());
  bts::SpsmBitmapOptions options(body);
  form->addWidget(&options);
  assert(options.isChecked());
  const auto defaults = options.policy_tags();
  assert(defaults.contains("spsm:policy:bitmap-color=native-linear-rec709-rgb-levels-ignore-a"));
  assert(defaults.contains("spsm:policy:bitmap-bytes=little-endian"));
  assert(defaults.contains("spsm:policy:bitmap-rows=first-row-v0"));
  assert(defaults.contains("spsm:policy:levels=native-scalar"));
  assert(defaults.contains("spsm:policy:mask-bitmap=native-ordered-fill-levels-raw-r-ignore-a"));
  options.findChild<QCheckBox *>("spsmNativeScalarLevels")->setChecked(false);
  options.findChild<QCheckBox *>("spsmNativeBitmapMask")->setChecked(false);
  for (const char *name : {"spsmBitmapColor",
                           "spsmBitmapBytes",
                           "spsmBitmapRows",
                           "spsmBitmapAlpha",
                           "spsmBitmapUv",
                           "spsmBitmapSampling"})
    options.findChild<QComboBox *>(name)->setCurrentIndex(0);
  options.setChecked(false);
  assert(options.policy_tags().empty());
  auto *color = options.findChild<QComboBox *>("spsmBitmapColor");
  auto *roughness = options.findChild<QCheckBox *>("spsmBitmapRoughness");
  assert(color && color->currentData() == "unknown");
  assert(roughness && roughness->isChecked());
  assert(!roughness->isEnabled() && !color->isEnabled());
  options.setChecked(true);
  auto tags = options.policy_tags();
  assert(tags.size() == 7);
  assert(tags.contains("spsm:policy:bitmap-color=unknown"));
  assert(tags.contains("spsm:policy:bitmap-bytes=unknown"));
  assert(tags.contains("spsm:policy:bitmap-rows=unknown"));
  assert(tags.contains("spsm:policy:bitmap-alpha=unknown"));
  assert(tags.contains("spsm:policy:bitmap-uv=unknown"));
  assert(tags.contains("spsm:policy:bitmap-sampling=unknown"));
  for (const char *name : {"spsmBitmapBytes",
                           "spsmBitmapRows",
                           "spsmBitmapAlpha",
                           "spsmBitmapUv",
                           "spsmBitmapSampling"})
  {
    auto *combo = options.findChild<QComboBox *>(name);
    assert(combo);
    assert(combo->currentIndex() == 0);
    combo->setCurrentIndex(1);
  }
  const QStringList selected{"spsm:policy:bitmap-roughness=raw-r-ignore-a",
                             "spsm:policy:bitmap-color=unknown",
                             "spsm:policy:bitmap-bytes=little-endian",
                             "spsm:policy:bitmap-rows=first-row-v0",
                             "spsm:policy:bitmap-alpha=data-components",
                             "spsm:policy:bitmap-uv=projection0-centered-repeat-factors",
                             "spsm:policy:bitmap-sampling=hq-repeat"};
  assert(options.policy_tags() == selected);
  auto *mask = options.findChild<QCheckBox *>("spsmNativeBitmapMask");
  assert(mask && !mask->isChecked());
  mask->setChecked(true);
  assert(options.policy_tags().size() == 8);
  assert(options.policy_tags().contains(
      "spsm:policy:mask-bitmap=native-ordered-fill-levels-raw-r-ignore-a"));
  assert(!options.policy_tags().contains("spsm:policy:levels=native-scalar"));
  options.setChecked(false);
  assert(options.policy_tags().empty());
  options.setChecked(true);
  mask->setChecked(false);
  assert(options.policy_tags() == selected);
  auto *levels = options.findChild<QCheckBox *>("spsmNativeScalarLevels");
  assert(levels && !levels->isChecked());
  levels->setChecked(true);
  assert(options.policy_tags().size() == 8);
  assert(options.policy_tags().contains("spsm:policy:levels=native-scalar"));
  options.setChecked(false);
  assert(options.policy_tags().empty());
  options.setChecked(true);
  levels->setChecked(false);
  assert(options.policy_tags() == selected);
  options.setChecked(false);
  assert(options.policy_tags().empty());
  options.setChecked(true);
  assert(options.policy_tags() == selected);
  auto *rows = options.findChild<QComboBox *>("spsmBitmapRows");
  rows->setCurrentIndex(rows->findData("first-row-v1"));
  assert(options.policy_tags().contains("spsm:policy:bitmap-rows=first-row-v1"));
  for (const char *policy :
       {"native-linear-rec709-rgb-levels-ignore-a", "native-acescg-rgb-levels-ignore-a"})
  {
    const int index = color->findData(QString::fromLatin1(policy));
    assert(index > 0);
    color->setCurrentIndex(index);
    assert(options.policy_tags().contains("spsm:policy:bitmap-color=" + QString(policy)));
    assert(options.policy_tags().contains("spsm:policy:bitmap-roughness=raw-r-ignore-a"));
    levels->setChecked(true);
    mask->setChecked(true);
    roughness->setChecked(false);
    assert(!levels->isEnabled() && !mask->isEnabled());
    assert(options.policy_tags().contains("spsm:policy:bitmap-roughness=unknown"));
    assert(options.policy_tags().contains("spsm:policy:bitmap-color=" + QString(policy)));
    assert(!options.policy_tags().contains("spsm:policy:levels=native-scalar"));
    assert(!options.policy_tags().contains(
        "spsm:policy:mask-bitmap=native-ordered-fill-levels-raw-r-ignore-a"));
    options.setChecked(false);
    assert(options.policy_tags().empty());
    options.setChecked(true);
    assert(color->currentData() == policy && !roughness->isChecked());
    assert(!levels->isEnabled() && !mask->isEnabled());
    roughness->setChecked(true);
    assert(levels->isEnabled() && mask->isEnabled());
    assert(options.policy_tags().contains("spsm:policy:levels=native-scalar"));
    assert(options.policy_tags().contains(
        "spsm:policy:mask-bitmap=native-ordered-fill-levels-raw-r-ignore-a"));
    levels->setChecked(false);
    mask->setChecked(false);
  }
  color->setCurrentIndex(0);
  assert(options.policy_tags().contains("spsm:policy:bitmap-color=unknown"));
  // Test the component in a narrow, scrollable form; this is not a claim about
  // the complete production import dialog or source appearance.
  preview.resize(560, 545);
  preview.show();
  app.processEvents();
  assert(preview.width() == 560);
  assert(body->width() <= scroll->viewport()->width());
  auto *hint = options.findChild<QLabel *>("spsmBitmapInterpretationHint");
  assert(hint &&
         hint->height() >= hint->fontMetrics().lineSpacing() * int(hint->text().count('\n') + 1));
  assert(hint->geometry().bottom() < roughness->geometry().top());
  for (auto *combo : options.findChildren<QComboBox *>()) {
    const auto bounds = QRect(combo->mapTo(body, QPoint(0, 0)), combo->size());
    assert(bounds.left() >= 0 && bounds.right() < body->width());
    assert(combo->width() >= 120);
  }
  if (argc > 1) {
    color->setCurrentIndex(color->findData("native-linear-rec709-rgb-levels-ignore-a"));
    rows->setCurrentIndex(rows->findData("first-row-v0"));
    app.processEvents();
    assert(preview.grab().save(QString::fromLocal8Bit(argv[1])));
  }
  std::cout << "SPSM source UI: group off; Color Unknown; independent Roughness, named Color, "
               "AO and masks; seven stable tags; narrow form and reversible opt-in PASS\n";
}
