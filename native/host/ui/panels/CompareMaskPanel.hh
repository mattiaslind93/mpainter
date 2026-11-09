// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.hh"
#include "NumericLocale.hh"
#pragma once
#include "TextureDocument.hh"
#include "CompareRampEditor.hh"
#include <QCheckBox>
#include "HistogramMetadata.hh"
#include <QTimer>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <functional>
namespace bts {
class CompareMaskHistogram final : public QWidget {
 public:
  explicit CompareMaskHistogram(QWidget *parent) : QWidget(parent)
  {
    setMinimumHeight(70);
    setMaximumHeight(90);
  }
  std::array<std::array<int, 64>, 2> bins{};

 protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter p(this);
    p.fillRect(rect(), QColor(bts::theme::Canvas));
    const int bottom = height() - 18;
    int maximum = 1;
    for (const auto &series : bins)
      for (int value : series)
        maximum = std::max(maximum, value);
    const std::array<QColor, 2> colors{bts::theme::alpha(bts::theme::Success, 180),
                                       bts::theme::alpha(bts::theme::PlotPurple, 180)};
    for (int source = 0; source < 2; ++source) {
      p.setPen(Qt::NoPen);
      p.setBrush(colors[source]);
      for (int i = 0; i < 64; ++i)
        p.drawRect(QRectF(double(i) * width() / 64,
                          bottom - double(bins[source][i]) * (bottom - 5) / maximum,
                          std::max(1., double(width()) / 64),
                          double(bins[source][i]) * (bottom - 5) / maximum));
    }
    p.setPen(QColor(bts::theme::Text));
    for (int i = 0; i < 4; ++i)
      p.drawText(QRect(i * width() / 4, bottom, width() / 4, 18),
                 Qt::AlignLeft | Qt::AlignVCenter,
                 QString::number(i * .25));
  }
};
class CompareMaskPanel final : public QWidget {
 public:
  explicit CompareMaskPanel(QWidget *parent) : QWidget(parent)
  {
    histogram_timer_ = new QTimer(this);
    histogram_timer_->setInterval(16);
    connect(histogram_timer_, &QTimer::timeout, this, [this] { poll_histogram(); });
    setObjectName("compareMaskProperties");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel("COMPARE MASK", this));
    channel_ = combo("compareChannel");
    layout->addWidget(channel_);
    label_ = new QLabel("Compare", this);
    layout->addWidget(label_);
    auto *operands = new QHBoxLayout;
    operands->setSpacing(4);
    left_ = combo("compareLeft");
    right_ = combo("compareRight");
    operation_ = combo("compareOperator");
    for (auto *source : {left_, right_})
      source->addItems({"Layers Below", "This Layer", "This Mask", "Constant"});
    operation_->addItems({"<", "≈", ">"});
    operation_->setFixedWidth(44);
    operation_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    operands->addWidget(left_, 1);
    operands->addWidget(operation_);
    operands->addWidget(right_, 1);
    layout->addLayout(operands);
    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    constant_ = field("compareConstant", .5);
    tolerance_ = field("compareTolerance", .1);
    hardness_ = field("compareHardness", .9);
    constant_label_ = new QLabel("Constant", this);
    tolerance_label_ = new QLabel("Tolerance", this);
    form->addRow(constant_label_, constant_);
    form->addRow(tolerance_label_, tolerance_);
    form->addRow("Hardness", hardness_);
    layout->addLayout(form);
    offset_ = field("compareOffset", 0);
    offset_->setRange(-65504, 65504);
    width_ = field("compareWidth", .1);
    width_->setRange(0, 65504);
    form->addRow("Position / offset", offset_);
    form->addRow("Transition width", width_);
    invert_ = new QCheckBox("Invert", this);
    invert_->setObjectName("compareInvert");
    layout->addWidget(invert_);
    ramp_ = new CompareRampEditor(this);
    ramp_->setObjectName("compareRamp");
    layout->addWidget(ramp_);
    connect(offset_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
      auto e = effect_;
      e.compare_offset = v;
      activate_ramp(e);
      publish(e);
    });
    connect(width_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
      auto e = effect_;
      e.compare_width = v;
      publish(e);
    });
    connect(invert_, &QCheckBox::toggled, this, [this](bool v) {
      auto e = effect_;
      e.compare_invert = v;
      activate_ramp(e);
      publish(e);
    });
    ramp_->changed = [this](const auto &points) {
      auto e = effect_;
      e.compare_ramp = points;
      activate_ramp(e);
      publish(e);
    };
    warning_ = new QLabel("Identical operands produce a white or black mask.", this);
    warning_->setObjectName("compareIdenticalWarning");
    warning_->setWordWrap(true);
    warning_->setStyleSheet(bts::theme::qss("color:@Warning@;"));
    layout->addWidget(warning_);
    layout->addWidget(new QLabel("Source Channels Histogram", this));
    histogram_ = new CompareMaskHistogram(this);
    histogram_->setObjectName("compareHistogram");
    layout->addWidget(histogram_);
    connect(channel_, &QComboBox::currentIndexChanged, this, [this](int i) {
      if (i >= 0) {
        auto e = effect_;
        e.compare_channel = channel_->itemData(i).toInt();
        publish(e);
      }
    });
    connect(left_, &QComboBox::currentIndexChanged, this, [this](int i) {
      if (i >= 0) {
        auto e = effect_;
        e.left = i;
        publish(e);
      }
    });
    connect(right_, &QComboBox::currentIndexChanged, this, [this](int i) {
      if (i >= 0) {
        auto e = effect_;
        e.right = i;
        publish(e);
      }
    });
    connect(operation_, &QComboBox::currentIndexChanged, this, [this](int i) {
      if (i >= 0) {
        auto e = effect_;
        e.operation = i;
        publish(e);
      }
    });
    connect(constant_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
      auto e = effect_;
      e.constant = value;
      publish(e);
    });
    connect(tolerance_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
      auto e = effect_;
      e.tolerance = value;
      publish(e);
    });
    connect(hardness_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
      auto e = effect_;
      e.hardness = value;
      if (e.compare_width >= 0)
        e.compare_width = 1 - value;
      publish(e);
    });
  }
  std::function<void(const MaskEffect &)> changed;
  std::function<std::uint64_t(const TextureSet &, const HistogramRequest &)> histogram_requested;
  std::function<std::optional<HistogramMetadata>(const QUuid &)> histogram_result;
  void synchronize(const TextureSet *set, const MaskEffect *effect)
  {
    setVisible(effect && set);
    if (!effect || !set) {
      histogram_timer_->stop();
      return;
    }
    effect_ = *effect;
    const QSignalBlocker a(channel_), b(left_), c(right_), d(operation_), e(constant_),
        f(tolerance_), g(hardness_), h(offset_), j(width_), k(invert_);
    offset_->setValue(effect->compare_offset);
    width_->setValue(effect->compare_width < 0 ? 1 - effect->hardness : effect->compare_width);
    invert_->setChecked(effect->compare_invert);
    ramp_->points = effect->compare_ramp;
    ramp_->update();
    channel_->clear();
    const std::array<const char *, 6> names{
        "Base Color", "Metallic", "Roughness", "Normal", "Height / Bump", "Ambient occlusion"};
    for (int i = 0; i < 6; ++i)
      if (i != 3 && (i == 5 ? set->ao_channel : set->channels[i]))
        channel_->addItem(names[i], i);
    channel_->setCurrentIndex(channel_->findData(effect->compare_channel));
    channel_->setEnabled(effect->left != 3 || effect->right != 3);
    left_->setCurrentIndex(effect->left);
    right_->setCurrentIndex(effect->right);
    operation_->setCurrentIndex(effect->operation);
    constant_->setValue(effect->constant);
    tolerance_->setValue(effect->tolerance);
    hardness_->setValue(effect->hardness);
    const bool constant = effect->left == 3 || effect->right == 3,
               tolerance = effect->operation == 1, identical = effect->left == effect->right;
    constant_->setVisible(constant);
    constant_label_->setVisible(constant);
    tolerance_->setVisible(tolerance);
    tolerance_label_->setVisible(tolerance);
    label_->setText(identical ? "Compare *" : "Compare");
    warning_->setVisible(identical);
    if (histogram_requested) {
      HistogramRequest request;
      request.layer = set->selected_layer;
      request.effect = effect->id;
      request.mask = request.compare = true;
      request.channel = effect->compare_channel;
      histogram_ticket_ = histogram_requested(*set, request);
      histogram_->setProperty("gpuHistogramReady", false);
      histogram_timer_->start();
      poll_histogram();
    }
  }

 protected:
  void showEvent(QShowEvent *event) override
  {
    QWidget::showEvent(event);
    if (histogram_ticket_)
      histogram_timer_->start();
  }

 private:
  QTimer *histogram_timer_ = nullptr;
  std::uint64_t histogram_ticket_ = 0;
  void poll_histogram()
  {
    if (!isVisible()) {
      histogram_timer_->stop();
      return;
    }
    if (!histogram_result)
      return;
    const auto result = histogram_result(effect_.id);
    if (!result || result->ticket != histogram_ticket_)
      return;
    if (!result->valid) {
      histogram_->setToolTip("Histogram unavailable — see Console");
      histogram_timer_->stop();
      return;
    }
    histogram_->setToolTip({});
    for (int s = 0; s < 2; ++s)
      for (int i = 0; i < 64; ++i)
        histogram_->bins[s][i] = result->bins[s][i];
    QVariantList series;
    for (const auto &bins : result->bins) {
      QVariantList values;
      for (unsigned value : bins)
        values.append(value);
      series.append(QVariant(values));
    }
    histogram_->setProperty("gpuHistogramBins", series);
    histogram_->setProperty("gpuHistogramReady", true);
    histogram_->update();
    histogram_timer_->stop();
  }
  static void activate_ramp(MaskEffect &e)
  {
    if (e.compare_width < 0)
      e.compare_width = 1 - e.hardness;
  }
  QDoubleSpinBox *offset_, *width_;
  QCheckBox *invert_;
  CompareRampEditor *ramp_;
  MaskEffect effect_;
  QComboBox *channel_, *left_, *right_, *operation_;
  QDoubleSpinBox *constant_, *tolerance_, *hardness_;
  QLabel *constant_label_, *tolerance_label_, *label_, *warning_;
  CompareMaskHistogram *histogram_;
  QComboBox *combo(const char *name)
  {
    auto *c = new QComboBox(this);
    c->setObjectName(name);
    c->setMinimumWidth(0);
    c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    c->setMinimumContentsLength(1);
    c->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return c;
  }
  QDoubleSpinBox *field(const char *name, double initial)
  {
    auto *f = new bts::NumericSpinBox(this);
    f->setObjectName(name);
    f->setRange(0, 1);
    f->setDecimals(3);
    f->setSingleStep(.05);
    f->setKeyboardTracking(false);
    f->setValue(initial);
    return f;
  }
  void publish(const MaskEffect &effect)
  {
    effect_ = effect;
    if (changed)
      changed(effect);
  }
};
}  // namespace bts
