// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QComboBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QFontMetrics>
#include <QGroupBox>
#include <QLabel>
#include <QSizePolicy>
#include <QStringList>
#include <initializer_list>
#include <utility>

namespace bts {
// These are explicit interpretation choices, not translations of undocumented
// source enum ordinals. Unknown leaves the corresponding source branch off.
class SpsmBitmapOptions final : public QGroupBox {
 public:
  explicit SpsmBitmapOptions(QWidget *parent = nullptr)
      : QGroupBox("Embedded bitmaps — explicit interpretation", parent)
  {
    setObjectName("spsmBitmapOptions");
    setCheckable(true);
    setChecked(true);
    auto *form = new QFormLayout(this);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *hint = new QLabel(
        "Native interpretation; unsupported subactions are reported.\n"
        "Explicit assumptions — compare the result in Painter.\n"
        "Original masks are preserved: a black parent mask stays black.",
        this);
    hint->setObjectName("spsmBitmapInterpretationHint");
    hint->setToolTip(
        "Only compatible independent RGBA16F bitmap fills are supported. "
        "Color requires a separate named linear RGB interpretation. "
        "Normal stays internal; compiled packages are never executed. "
        "Source appearance is unverified.");
    hint->setWordWrap(true);
    // A compact outer QFormLayout can squeeze a word-wrapped label to one
    // line. Preserve its explicit lines; extra wrapping may still grow it.
    hint->setMinimumHeight(hint->fontMetrics().lineSpacing() * int(hint->text().count('\n') + 1));
    form->addRow(hint);
    roughness_ = new QCheckBox("Read Roughness from raw R (ignore alpha)", this);
    roughness_->setObjectName("spsmBitmapRoughness");
    // The enclosing group is off by default. This retains the old explicit
    // group opt-in while keeping Color and Roughness independently selectable.
    roughness_->setChecked(true);
    form->addRow(roughness_);
    color_ = choice(form,
                    "Color interpretation",
                    "spsmBitmapColor",
                    {{"Unknown — leave Color disabled", "unknown"},
                     {"Linear Rec.709 RGB", "native-linear-rec709-rgb-levels-ignore-a"},
                     {"ACEScg RGB", "native-acescg-rgb-levels-ignore-a"}});
    color_->setToolTip(
        "Explicit native interpretation: ignore stored alpha; apply "
        "ordered RGB Levels in the chosen linear input space, then "
        "convert to ACEScg. This does not identify the source color "
        "space or establish Painter appearance parity. The storage, "
        "UV and sampling choices below are also required.");
    bytes_ = choice(form,
                    "Stored byte order",
                    "spsmBitmapBytes",
                    {{"Unknown — do not decode", "unknown"},
                     {"Little endian", "little-endian"},
                     {"Big endian", "big-endian"}});
    rows_ = choice(form,
                   "Stored first row",
                   "spsmBitmapRows",
                   {{"Unknown — do not decode", "unknown"},
                    {"UV V = 0", "first-row-v0"},
                    {"UV V = 1", "first-row-v1"}});
    alpha_ = choice(form,
                    "Components",
                    "spsmBitmapAlpha",
                    {{"Unknown — do not decode", "unknown"},
                     {"Raw components, ignore alpha", "data-components"}});
    uv_ = choice(form,
                 "UV interpretation",
                 "spsmBitmapUv",
                 {{"Unknown — do not decode", "unknown"},
                  {"Centered scale factors, repeat", "projection0-centered-repeat-factors"}});
    sampling_ = choice(form,
                       "Native sampling",
                       "spsmBitmapSampling",
                       {{"Unknown — do not decode", "unknown"}, {"HQ repeat", "hq-repeat"}});
    levels_ = new QCheckBox("Use native scalar Levels interpretation", this);
    levels_->setObjectName("spsmNativeScalarLevels");
    levels_->setChecked(true);
    levels_->setToolTip(
        "Explicit assumption for compatible Roughness chains: "
        "reciprocal gamma; source clamp 0 is off, 1 clamps the final "
        "value. Unsupported dependencies remain disabled and masks "
        "are preserved. Painter appearance is unverified.");
    form->addRow(levels_);
    mask_ = new QCheckBox("Use native bitmap masks (Fill + Levels)", this);
    mask_->setObjectName("spsmNativeBitmapMask");
    mask_->setChecked(true);
    mask_->setToolTip(
        "Explicit native interpretation of compatible ordered bitmap mask "
        "Fill/Levels chains. Requires the bitmap choices above. Raw R, "
        "ignore alpha; the final mask is clamped to coverage. Unsupported "
        "dependencies remain Source appearance is unverified.");
    form->addRow(mask_);
    QObject::connect(roughness_, &QCheckBox::toggled, this, [this](bool enabled) {
      levels_->setEnabled(enabled);
      mask_->setEnabled(enabled);
    });
  }

  QStringList policy_tags() const
  {
    if (!isChecked())
      return {};
    QStringList tags{"spsm:policy:bitmap-roughness=" +
                         QString(roughness_->isChecked() ? "raw-r-ignore-a" : "unknown"),
                     "spsm:policy:bitmap-color=" + color_->currentData().toString(),
                     "spsm:policy:bitmap-bytes=" + bytes_->currentData().toString(),
                     "spsm:policy:bitmap-rows=" + rows_->currentData().toString(),
                     "spsm:policy:bitmap-alpha=" + alpha_->currentData().toString(),
                     "spsm:policy:bitmap-uv=" + uv_->currentData().toString(),
                     "spsm:policy:bitmap-sampling=" + sampling_->currentData().toString()};
    if (roughness_->isChecked() && levels_->isChecked())
      tags.append("spsm:policy:levels=native-scalar");
    if (roughness_->isChecked() && mask_->isChecked())
      tags.append("spsm:policy:mask-bitmap=native-ordered-fill-levels-raw-r-ignore-a");
    return tags;
  }

 private:
  using Choices = std::initializer_list<std::pair<const char *, const char *>>;
  QComboBox *choice(QFormLayout *form, const char *label, const char *name, Choices choices)
  {
    auto *combo = new QComboBox(this);
    combo->setObjectName(QString::fromLatin1(name));
    combo->setMinimumContentsLength(15);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    for (const auto &[text, value] : choices)
      combo->addItem(QString::fromUtf8(text), QString::fromLatin1(value));
    combo->setCurrentIndex(1);
    form->addRow(QString::fromLatin1(label), combo);
    return combo;
  }
  QComboBox *color_, *bytes_, *rows_, *alpha_, *uv_, *sampling_;
  QCheckBox *roughness_, *levels_, *mask_;
};
}  // namespace bts
