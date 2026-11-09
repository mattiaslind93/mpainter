#include "Theme.hh"
#include "SliderHitArea.hh"
#pragma once
#include "LayerEffect.hh"
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QTabletEvent>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
namespace bts {
// The same input/output handle geometry is used by Levels and Levels/Ramp.
// Numeric fields remain the precision/HDR route; handles edit the visible 0..1 range.
class HandleRamp final : public QWidget {
 public:
  explicit HandleRamp(QWidget *parent = nullptr) : QWidget(parent)
  {
    new SliderHandleGeometry(this, [this] {
      QList<QRectF> result;
      for (int i = 0; i < int(positions.size()); ++i)
        result.append(QRectF(handle_position(i) - QPointF(7, 7), QSizeF(14, 14)));
      return result;
    });
    setMinimumHeight(48);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
  }
  std::vector<float> positions{0, 1};
  std::vector<QString> labels;
  std::function<void(int, float)> moved;
  std::function<QString(float)> formatted;
  std::function<void()> started, finished;
  QPointF handle_position(int index) const
  {
    return {9 + std::clamp(positions.at(index), 0.f, 1.f) * std::max(1, width() - 18), 32};
  }

 protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF bar(9, 27, std::max(1, width() - 18), 10);
    QLinearGradient gradient(bar.topLeft(), bar.topRight());
    gradient.setColorAt(0, Qt::black);
    gradient.setColorAt(1, Qt::white);
    p.setPen(QPen(QColor(bts::theme::Hover), 1));
    p.setBrush(gradient);
    p.drawRoundedRect(bar, bts::theme::RadiusPx, bts::theme::RadiusPx);
    for (int i = 0; i < int(positions.size()); ++i) {
      const auto center = handle_position(i);
      const auto color = positions.size() == 1          ? QColor(bts::theme::Heading) :
                         i == 0                         ? QColor(bts::theme::Canvas) :
                         i == int(positions.size()) - 1 ? QColor(bts::theme::Heading) :
                                                          QColor(bts::theme::Muted);
      p.setPen(QColor(bts::theme::Text));
      const auto text = i < int(labels.size()) ? labels[i] :
                        formatted              ? formatted(positions[i]) :
                                                 QString::number(positions[i], 'f', 3);
      const auto width = p.fontMetrics().horizontalAdvance(text);
      p.drawText(
          QPointF(
              std::clamp(center.x() - width / 2., 0., double(std::max(0, this->width() - width))),
              16),
          text);
      p.setPen(QPen(i == active_ ? QColor(bts::theme::Accent) : QColor(bts::theme::Text),
                    i == active_ ? 2 : 1));
      p.setBrush(color);
      p.drawEllipse(center, 6.5, 6.5);
    }
  }
  void mousePressEvent(QMouseEvent *event) override
  {
    if (event->button() == Qt::LeftButton) {
      begin(event->position());
      event->accept();
    }
  }
  void mouseMoveEvent(QMouseEvent *event) override
  {
    if (active_ >= 0 && (event->buttons() & Qt::LeftButton)) {
      move(event->position());
      event->accept();
    }
  }
  void mouseReleaseEvent(QMouseEvent *event) override
  {
    if (event->button() == Qt::LeftButton && active_ >= 0) {
      move(event->position());
      end();
      event->accept();
    }
  }
  void tabletEvent(QTabletEvent *event) override
  {
    if (event->type() == QEvent::TabletPress)
      begin(event->position());
    else if (event->type() == QEvent::TabletMove && active_ >= 0)
      move(event->position());
    else if (event->type() == QEvent::TabletRelease && active_ >= 0) {
      move(event->position());
      end();
    }
    event->accept();
  }

 private:
  int active_ = -1;
  void begin(QPointF point)
  {
    if (!isEnabled())
      return;
    setFocus();
    active_ = 0;
    for (int i = 1; i < int(positions.size()); ++i)
      if (std::abs(point.x() - handle_position(i).x()) <
          std::abs(point.x() - handle_position(active_).x()))
        active_ = i;
    if (started)
      started();
    move(point);
  }
  void move(QPointF point)
  {
    if (active_ < 0)
      return;
    const float value = std::clamp(float(point.x() - 9) / std::max(1, width() - 18), 0.f, 1.f);
    positions[active_] = value;
    if (moved)
      moved(active_, value);
    update();
  }
  void end()
  {
    active_ = -1;
    update();
    if (finished)
      finished();
  }
};
class NormalizedHistogram final : public QWidget {
 public:
  explicit NormalizedHistogram(QWidget *parent = nullptr) : QWidget(parent)
  {
    setMinimumHeight(90);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }
  std::array<unsigned, 64> bins{};
  QRectF plot_rect() const
  {
    return QRectF(9, 5, std::max(1, width() - 18), std::max(1, height() - 15));
  }
  double bar_height(int i) const
  {
    const auto high = *std::max_element(bins.begin(), bins.end());
    return high ? std::sqrt(double(bins.at(i)) / high) * plot_rect().height() : 0;
  }

 protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter p(this);
    p.fillRect(rect(), QColor(bts::theme::Canvas));
    const auto plot = plot_rect();
    p.setPen(QColor(bts::theme::Raised));
    for (int i = 1; i < 4; ++i)
      p.drawLine(QPointF(plot.left() + plot.width() * i / 4, plot.top()),
                 QPointF(plot.left() + plot.width() * i / 4, plot.bottom()));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(bts::theme::Text));
    const auto step = plot.width() / 64;
    for (int i = 0; i < 64; ++i) {
      const auto h = bar_height(i);
      if (h > 0)
        p.drawRect(QRectF(plot.left() + step * i, plot.bottom() - h, std::max(1., step - .4), h));
    }
    p.setPen(QColor(bts::theme::Disabled));
    p.drawLine(plot.bottomLeft(), plot.bottomRight());
  }
};
class LevelsRampEditor final : public QWidget {
 public:
  explicit LevelsRampEditor(QWidget *parent = nullptr) : QWidget(parent)
  {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(bts::theme::Space);
    auto *input_label = new QLabel("Input", this);
    input_label->setStyleSheet(bts::theme::qss("color:@Muted@"));
    layout->addWidget(input_label);
    input = new HandleRamp(this);
    input->setObjectName("levelsInputRamp");
    input->setAccessibleName("Input black, gamma and white handles");
    input->setToolTip(
        "Drag the black, gray and white input handles. Exact and HDR values are available below.");
    layout->addWidget(input);
    histogram = new NormalizedHistogram(this);
    histogram->setObjectName("levelsHistogramPlot");
    layout->addWidget(histogram);
    auto *output_label = new QLabel("Output", this);
    output_label->setStyleSheet(bts::theme::qss("color:@Muted@"));
    layout->addWidget(output_label);
    output = new HandleRamp(this);
    output->setObjectName("levelsOutputRamp");
    output->setAccessibleName("Output black and white handles");
    output->setToolTip("Drag either output endpoint; endpoints may cross to invert the range.");
    layout->addWidget(output);
    input->moved = [this](int handle, float value) {
      auto next = curve_;
      if (handle == 0)
        next.input_low = std::min(
            value, std::min(next.input_high - 1e-4f, std::nextafter(next.input_high, -INFINITY)));
      else if (handle == 2)
        next.input_high = std::max(
            value, std::max(next.input_low + 1e-4f, std::nextafter(next.input_low, INFINITY)));
      else {
        const float relative = std::clamp(
            (value - next.input_low) / (next.input_high - next.input_low), .001f, .999f);
        next.gamma = std::clamp(std::log(relative) / std::log(.5f), .01f, maximum_gamma);
      }
      publish(next);
    };
    output->moved = [this](int handle, float value) {
      auto next = curve_;
      (handle == 0 ? next.output_low : next.output_high) = value;
      publish(next);
    };
    for (auto *ramp : {input, output}) {
      ramp->started = [this] {
        if (edit_started)
          edit_started();
      };
      ramp->finished = [this] {
        if (edit_finished)
          edit_finished();
      };
    }
  }
  HandleRamp *input, *output;
  NormalizedHistogram *histogram;
  float maximum_gamma = 100;
  std::function<void(const LevelCurve &)> changed;
  std::function<void()> edit_started, edit_finished;
  void set_curve(const LevelCurve &curve)
  {
    curve_ = curve;
    input->labels = {QString::number(curve.input_low, 'g', 4),
                     QString::number(curve.gamma, 'g', 4),
                     QString::number(curve.input_high, 'g', 4)};
    input->positions = {curve.input_low,
                        curve.input_low +
                            (curve.input_high - curve.input_low) * std::pow(.5f, curve.gamma),
                        curve.input_high};
    output->positions = {curve.output_low, curve.output_high};
    input->update();
    output->update();
  }
  const LevelCurve &curve() const
  {
    return curve_;
  }
  void set_histogram(const std::array<unsigned, 64> &bins)
  {
    histogram->bins = bins;
    histogram->update();
  }
  void set_histogram_visible(bool visible)
  {
    histogram->setVisible(visible);
  }

 private:
  LevelCurve curve_;
  void publish(const LevelCurve &curve)
  {
    set_curve(curve);
    if (changed)
      changed(curve_);
  }
};
}  // namespace bts
