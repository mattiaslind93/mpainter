// SPDX-License-Identifier: GPL-3.0-or-later
#include "NumericLocale.hh"
#pragma once
#include "Theme.hh"
#include "BrushSettings.hh"
#include "Performance.hh"
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QToolButton>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <functional>
#include <optional>
#include <tuple>
namespace bts {
class AlphaPanel final : public QWidget {
 public:
  std::function<void()> cleared, import_requested;
  std::function<void(const DynamicStrokeParameters &)> dynamic_changed;
  std::function<void(bool, double, double)> parameters_changed;
  explicit AlphaPanel(QWidget *parent) : QWidget(parent)
  {
    setObjectName("alphaPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(bts::theme::Space);
    /* The heading belongs to the PropertySection around this panel. */ auto *card =
        new QHBoxLayout;
    preview_ = new QLabel(this);
    preview_->setObjectName("alphaThumbnailPreview");
    preview_->setFixedSize(48, 48);
    card->addWidget(preview_);
    name_ = new QLabel(this);
    name_->setObjectName("alphaResourceName");
    name_->setWordWrap(true);
    card->addWidget(name_, 1);
    auto *clear = new QPushButton("×", this);
    clear->setObjectName("clearBrushAlpha");
    clear->setToolTip("Clear to uniform color");
    clear->setFixedWidth(24);
    card->addWidget(clear);
    layout->addLayout(card);
    connect(clear, &QPushButton::clicked, this, [this] {
      if (cleared)
        cleared();
    });
    auto *import = new QPushButton("Import alpha…", this);
    import->setObjectName("importBrushAlpha");
    layout->addWidget(import);
    connect(import, &QPushButton::clicked, this, [this] {
      if (import_requested)
        import_requested();
    });
    dynamic_section_ = new QWidget(this);
    auto *section = new QVBoxLayout(dynamic_section_);
    section->setContentsMargins(0, 0, 0, 0);
    dynamic_toggle_ = new QToolButton(dynamic_section_);
    dynamic_toggle_->setObjectName("dynamicStrokeSection");
    dynamic_toggle_->setText("Dynamic Stroke");
    dynamic_toggle_->setCheckable(true);
    dynamic_toggle_->setArrowType(Qt::RightArrow);
    dynamic_toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    section->addWidget(dynamic_toggle_);
    dynamic_body_ = new QWidget(dynamic_section_);
    auto *body = new QVBoxLayout(dynamic_body_);
    body->setContentsMargins(0, 0, 0, 0);
    body->addWidget(new QLabel("Dynamic Controls: Stamp Index", dynamic_body_));
    auto *start_row = new QHBoxLayout;
    start_row->addWidget(new QLabel("Stamp Start", dynamic_body_));
    dynamic_start_ = new QComboBox(dynamic_body_);
    dynamic_start_->setObjectName("dynamicStampStart");
    dynamic_start_->addItems({"From Beginning (0)", "From Random Index"});
    start_row->addWidget(dynamic_start_, 1);
    body->addLayout(start_row);
    auto *cycle_row = new QHBoxLayout;
    cycle_row->addWidget(new QLabel("Stamp Cycle Count", dynamic_body_));
    dynamic_cycle_ = new QSpinBox(dynamic_body_);
    dynamic_cycle_->setObjectName("dynamicStampCycle");
    dynamic_cycle_->setRange(1, 1024);
    dynamic_cycle_->setKeyboardTracking(false);
    cycle_row->addWidget(dynamic_cycle_);
    body->addLayout(cycle_row);
    dynamic_cycle_slider_ = new QSlider(Qt::Horizontal, dynamic_body_);
    dynamic_cycle_slider_->setObjectName("dynamicStampCycleSlider");
    dynamic_cycle_slider_->setRange(1, 1024);
    body->addWidget(dynamic_cycle_slider_);
    section->addWidget(dynamic_body_);
    dynamic_body_->hide();
    section->addWidget(new QLabel("Parameters", dynamic_section_));
    const auto parameter = [&](const char *label, const char *name, double low) {
      auto *line = new QHBoxLayout;
      line->addWidget(new QLabel(label, dynamic_section_));
      auto *spin = new bts::NumericSpinBox(dynamic_section_);
      spin->setObjectName(name);
      spin->setRange(low, 1);
      spin->setDecimals(3);
      spin->setSingleStep(.05);
      spin->setKeyboardTracking(false);
      line->addWidget(spin);
      section->addLayout(line);
      return spin;
    };
    dynamic_roundness_ = parameter("Roundness", "dynamicRoundness", 0);
    dynamic_width_ = parameter("Width", "dynamicWidth", .01);
    layout->addWidget(dynamic_section_);
    connect(dynamic_toggle_, &QToolButton::toggled, this, [this](bool on) {
      dynamic_body_->setVisible(on);
      dynamic_toggle_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });
    const auto dynamic_update = [this] {
      DynamicStrokeParameters value;
      value.cycle = dynamic_cycle_->value();
      value.random_start = dynamic_start_->currentIndex() == 1;
      value.roundness = float(dynamic_roundness_->value());
      value.width = float(dynamic_width_->value());
      if (dynamic_changed)
        dynamic_changed(value);
    };
    connect(dynamic_start_, &QComboBox::currentIndexChanged, this, dynamic_update);
    connect(dynamic_cycle_, &QSpinBox::valueChanged, this, dynamic_update);
    connect(dynamic_roundness_, &QDoubleSpinBox::valueChanged, this, dynamic_update);
    connect(dynamic_width_, &QDoubleSpinBox::valueChanged, this, dynamic_update);
    connect(dynamic_cycle_slider_, &QSlider::valueChanged, dynamic_cycle_, &QSpinBox::setValue);
    connect(dynamic_cycle_, &QSpinBox::valueChanged, dynamic_cycle_slider_, &QSlider::setValue);
    uniform_row_ = new QWidget(this);
    auto *u = new QHBoxLayout(uniform_row_);
    u->setContentsMargins(0, 0, 0, 0);
    u->addWidget(new QLabel("Grayscale", this));
    uniform_ = new bts::NumericSpinBox(this);
    uniform_->setObjectName("alphaUniform");
    uniform_->setRange(0, 1);
    uniform_->setDecimals(3);
    uniform_->setSingleStep(.05);
    uniform_->setKeyboardTracking(false);
    u->addWidget(uniform_);
    layout->addWidget(uniform_row_);
    hardness_row_ = new QWidget(this);
    auto *h = new QHBoxLayout(hardness_row_);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(new QLabel("Hardness", this));
    hardness_ = new bts::NumericSpinBox(this);
    hardness_->setObjectName("alphaHardness");
    hardness_->setRange(0, 1);
    hardness_->setDecimals(3);
    hardness_->setSingleStep(.05);
    hardness_->setKeyboardTracking(false);
    h->addWidget(hardness_);
    layout->addWidget(hardness_row_);
    invert_ = new QCheckBox("Invert", this);
    invert_->setObjectName("alphaInvert");
    layout->addWidget(invert_);
    stroke_ = new QLabel(this);
    stroke_->setObjectName("alphaStrokePreview");
    stroke_->setMinimumHeight(40);
    layout->addWidget(stroke_);
    layout->addWidget(new QLabel("Stamp blending: Normal", this));
    const auto changed = [this] {
      if (parameters_changed)
        parameters_changed(invert_->isChecked(), uniform_->value(), hardness_->value());
    };
    connect(invert_, &QCheckBox::toggled, this, changed);
    connect(uniform_, &QDoubleSpinBox::valueChanged, this, changed);
    connect(hardness_, &QDoubleSpinBox::valueChanged, this, changed);
  }
  static QImage thumbnail(const BrushAlpha &alpha, int size = 48, float hardness = 0)
  {
    QImage image(size, size, QImage::Format_RGB32);
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        const int value = qRound(
            255 * alpha.sample((x + .5) * 2 / size - 1, 1 - (y + .5) * 2 / size, hardness));
        image.setPixel(x, y, qRgb(value, value, value));
      }
    return image;
  }
  void synchronize(const BrushSettings &brush)
  {
    {
      const QSignalBlocker a(dynamic_start_), b(dynamic_cycle_), c(dynamic_cycle_slider_),
          d(dynamic_roundness_), e(dynamic_width_);
      const auto &value = brush.alpha.dynamic;
      dynamic_start_->setCurrentIndex(value.random_start ? 1 : 0);
      dynamic_cycle_->setValue(value.cycle);
      dynamic_cycle_slider_->setValue(value.cycle);
      dynamic_roundness_->setValue(value.roundness);
      dynamic_width_->setValue(value.width);
    }
    dynamic_section_->setVisible(brush.alpha.kind == AlphaKind::DynamicOutline);
    const auto &alpha = brush.alpha;
    const QSignalBlocker a(uniform_), b(invert_), c(hardness_);
    uniform_->setValue(alpha.uniform);
    invert_->setChecked(alpha.inverted);
    hardness_->setValue(brush.hardness);
    name_->setText(QString::fromStdString(alpha.name()));
    uniform_row_->setVisible(alpha.kind == AlphaKind::Uniform);
    hardness_row_->setVisible(alpha.kind == AlphaKind::Shape);
    invert_->setVisible(alpha.kind == AlphaKind::Plus || alpha.kind == AlphaKind::Uniform);
    const auto thumb_key = sample_key(alpha, brush.hardness);
    const auto stroke_key = stroke_sample_key(brush);
    if (thumbnail_key_ && *thumbnail_key_ == thumb_key && stroke_key_ &&
        *stroke_key_ == stroke_key)
    {
      pending_.reset();
      return;
    }
    pending_ = brush;
    update();
  }

 protected:
  void paintEvent(QPaintEvent *event) override
  {
    render_pending();
    QWidget::paintEvent(event);
  }

 private:
  // AlphaBitmap is immutable; compare its retained owner rather than its pixels.
  using SampleKey = std::tuple<std::array<double, 7>, std::shared_ptr<const AlphaBitmap>>;
  using StrokeKey = std::tuple<SampleKey, std::array<double, 5>>;
  static SampleKey sample_key(const BrushAlpha &alpha, float hardness)
  {
    return {{double(alpha.kind),
             double(alpha.inverted),
             alpha.uniform,
             hardness,
             alpha.dynamic.roundness,
             alpha.dynamic.width,
             alpha.dynamic.phase},
            alpha.bitmap};
  }
  static StrokeKey stroke_sample_key(const BrushSettings &brush)
  {
    return {sample_key(brush.alpha, brush.hardness),
            {brush.spacing,
             brush.flow,
             brush.stroke_opacity,
             double(brush.alpha.dynamic.cycle),
             double(brush.alpha.dynamic.random_start)}};
  }
  std::optional<SampleKey> thumbnail_key_;
  std::optional<StrokeKey> stroke_key_;
  std::optional<BrushSettings> pending_;
  void render_pending()
  {
    if (!pending_)
      return;
    const auto brush = std::move(*pending_);
    pending_.reset();
    const auto &alpha = brush.alpha;
    const auto thumb_key = sample_key(alpha, brush.hardness);
    const auto stroke_key = stroke_sample_key(brush);
    if (!thumbnail_key_ || *thumbnail_key_ != thumb_key) {
      performance::Scope timing("alpha_thumbnail_ui");
      preview_->setPixmap(QPixmap::fromImage(thumbnail(alpha, 48, brush.hardness)));
      thumbnail_key_ = thumb_key;
    }
    if (!stroke_key_ || *stroke_key_ != stroke_key) {
      performance::Scope timing("alpha_stroke_swatch_ui");
      QImage stroke(200, 40, QImage::Format_RGB32);
      const double step = std::max(1.f, 24 * brush.spacing / 100);
      DynamicStrokeSampler dynamic(0x4254537072657669ULL);
      std::vector<std::pair<double, BrushAlpha>> dabs;
      for (double center = 12; center < 190; center += step) {
        auto resolved = alpha;
        if (alpha.kind == AlphaKind::DynamicOutline)
          resolved.dynamic.phase = dynamic.phase(alpha.dynamic);
        dabs.emplace_back(center, std::move(resolved));
      }
      for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 200; ++x) {
          float coverage = 0;
          for (const auto &[center, resolved] : dabs) {
            const float stamp = resolved.sample(
                                    (x + .5 - center) / 12, (19.5 - y) / 12, brush.hardness) *
                                brush.flow / 100;
            coverage = stamp + coverage * (1 - stamp);
          }
          const int value = qRound(255 * (.12 + .88 * coverage * brush.stroke_opacity / 100));
          stroke.setPixel(x, y, qRgb(value, value, value));
        }
      stroke_->setPixmap(QPixmap::fromImage(stroke));
      stroke_key_ = stroke_key;
    }
  }
  QWidget *dynamic_section_, *dynamic_body_;
  QToolButton *dynamic_toggle_;
  QComboBox *dynamic_start_;
  QSpinBox *dynamic_cycle_;
  QSlider *dynamic_cycle_slider_;
  QDoubleSpinBox *dynamic_roundness_, *dynamic_width_;
  QLabel *preview_, *name_, *stroke_;
  QDoubleSpinBox *uniform_, *hardness_;
  QCheckBox *invert_;
  QWidget *uniform_row_, *hardness_row_;
};
}  // namespace bts
