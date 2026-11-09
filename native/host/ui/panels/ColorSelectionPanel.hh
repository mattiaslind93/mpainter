#include "NumericLocale.hh"
#pragma once
#include "Theme.hh"
#include "TextureDocument.hh"
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <functional>
namespace bts {
class ColorSelectionPanel final : public QWidget {
 public:
  explicit ColorSelectionPanel(QWidget *parent) : QWidget(parent)
  {
    setObjectName("colorSelectionProperties");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(bts::theme::SpaceLoose);
    layout->addWidget(new QLabel("COLOR SELECTION", this));
    auto *resource = new QHBoxLayout;
    card_ = new QPushButton(this);
    card_->setObjectName("selectionIdMap");
    card_->setToolTip("Use this Texture Set's baked ID map");
    card_->setMinimumWidth(0);
    card_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    resource->addWidget(card_);
    auto *clear = new QPushButton("×", this);
    clear->setObjectName("selectionClearIdMap");
    clear->setFixedWidth(22);
    resource->addWidget(clear);
    layout->addLayout(resource);
    connect(card_, &QPushButton::clicked, this, [this] {
      auto e = effect_;
      e.use_id_map = true;
      publish(e);
    });
    connect(clear, &QPushButton::clicked, this, [this] {
      auto e = effect_;
      e.use_id_map = false;
      publish(e);
    });
    const std::array<const char *, 3> names{"Output value", "Hardness", "Tolerance"};
    const std::array<const char *, 3> objects{
        "selectionOutput", "selectionHardness", "selectionTolerance"};
    for (int i = 0; i < 3; ++i) {
      auto *row = new QHBoxLayout;
      row->addWidget(new QLabel(names[i], this));
      fields_[i] = new bts::NumericSpinBox(this);
      fields_[i]->setObjectName(objects[i]);
      fields_[i]->setAccessibleName(names[i]);
      fields_[i]->setRange(0, 1);
      fields_[i]->setDecimals(3);
      fields_[i]->setSingleStep(.05);
      fields_[i]->setKeyboardTracking(false);
      row->addWidget(fields_[i]);
      if (i == 0) {
        auto *invert = new QPushButton("Invert", this);
        invert->setObjectName("selectionInvertOutput");
        row->addWidget(invert);
        connect(invert, &QPushButton::clicked, this, [this] {
          fields_[0]->setValue(1 - fields_[0]->value());
        });
      }
      layout->addLayout(row);
      sliders_[i] = new QSlider(Qt::Horizontal, this);
      sliders_[i]->setRange(0, 1000);
      layout->addWidget(sliders_[i]);
      connect(sliders_[i], &QSlider::sliderReleased, this, [this, i] {
        fields_[i]->setValue(sliders_[i]->value() / 1000.);
      });
      connect(fields_[i], &QDoubleSpinBox::valueChanged, this, [this, i](double value) {
        auto e = effect_;
        if (i == 0)
          e.output = value;
        if (i == 1)
          e.hardness = value;
        if (i == 2)
          e.tolerance = value;
        publish(e);
      });
    }
    auto *colors = new QHBoxLayout;
    colors->addWidget(new QLabel("Colors", this));
    colors->addStretch();
    pick_ = new QPushButton("Pick color", this);
    pick_->setObjectName("selectionPickColor");
    pick_->setCheckable(true);
    colors->addWidget(pick_);
    layout->addLayout(colors);
    connect(pick_, &QPushButton::toggled, this, [this](bool active) {
      if (pick_changed)
        pick_changed(active);
    });
    colors_ = new QListWidget(this);
    colors_->setObjectName("selectionColors");
    colors_->setMaximumHeight(75);
    layout->addWidget(colors_);
    auto *remove = new QPushButton("Remove selected color", this);
    remove->setObjectName("selectionRemoveColor");
    layout->addWidget(remove);
    connect(remove, &QPushButton::clicked, this, [this] {
      const int row = colors_->currentRow();
      if (row >= 0 && row < int(effect_.colors.size())) {
        auto e = effect_;
        e.colors.erase(e.colors.begin() + row);
        publish(e);
      }
    });
  }
  std::function<void(const MaskEffect &)> changed;
  std::function<void(bool)> pick_changed;
  void set_picking(bool active)
  {
    const QSignalBlocker blocker(pick_);
    pick_->setChecked(active);
  }
  void synchronize(const TextureSet *set, const MaskEffect *effect)
  {
    setVisible(effect);
    if (!effect || !set)
      return;
    effect_ = *effect;
    const bool available = bool(set->mesh_maps[2]);
    card_->setText(effect->use_id_map && available ?
                       QString::fromStdString(set->mesh_maps[2]->name) :
                       "ID Mask: None");
    card_->setEnabled(available);
    pick_->setEnabled(available && effect->use_id_map);
    const std::array<float, 3> values{effect->output, effect->hardness, effect->tolerance};
    for (int i = 0; i < 3; ++i) {
      const QSignalBlocker a(fields_[i]), b(sliders_[i]);
      fields_[i]->setValue(values[i]);
      sliders_[i]->setValue(qRound(values[i] * 1000));
    }
    const int selected = colors_->currentRow();
    colors_->clear();
    for (const auto &rgb : effect->colors) {
      const QColor color = QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
      QPixmap swatch(20, 20);
      swatch.fill(color);
      auto *item = new QListWidgetItem(QIcon(swatch),
                                       QString("%1, %2, %3")
                                           .arg(rgb[0], 0, 'f', 3)
                                           .arg(rgb[1], 0, 'f', 3)
                                           .arg(rgb[2], 0, 'f', 3),
                                       colors_);
      item->setData(Qt::UserRole, QVariant::fromValue(color));
    }
    if (!effect->colors.empty())
      colors_->setCurrentRow(std::clamp(selected, 0, int(effect->colors.size()) - 1));
  }

 private:
  MaskEffect effect_;
  QPushButton *card_, *pick_;
  QListWidget *colors_;
  std::array<QDoubleSpinBox *, 3> fields_{};
  std::array<QSlider *, 3> sliders_{};
  void publish(const MaskEffect &effect)
  {
    effect_ = effect;
    if (changed)
      changed(effect);
  }
};
}  // namespace bts
