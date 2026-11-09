#pragma once
#include "TextureDocument.hh"
#include <QStandardItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <functional>
namespace bts {
// Metadata-only inspector; retain no document pointer across document swaps.
class GeneratorAnchorPanel final : public QWidget {
 public:
  GeneratorAnchorPanel(int slot, QWidget *parent) : QWidget(parent)
  {
    const auto suffix = QString::number(slot);
    setObjectName("generatorAnchorInput" + suffix);
    auto *form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    source_ = new QComboBox(this);
    source_->setObjectName("generatorAnchorSource" + suffix);
    form->addRow("Input source", source_);
    channel_ = new QComboBox(this);
    channel_->setObjectName("generatorAnchorChannel" + suffix);
    form->addRow("Source channel", channel_);
    component_ = new QComboBox(this);
    component_->setObjectName("generatorAnchorComponent" + suffix);
    component_->addItems({"RGB", "R", "G", "B", "Coverage"});
    form->addRow("Extract", component_);
    coverage_ = new QCheckBox("Use source coverage", this);
    coverage_->setObjectName("generatorAnchorCoverage" + suffix);
    form->addRow(coverage_);
    jump_ = new QPushButton("Go to anchor", this);
    jump_->setObjectName("generatorGoToAnchor" + suffix);
    form->addRow(jump_);
    warning_ = new QLabel(this);
    warning_->setObjectName("generatorAnchorWarning" + suffix);
    warning_->setWordWrap(true);
    form->addRow(warning_);
    for (auto *field : {source_, channel_}) {
      field->setMinimumWidth(0);
      field->setMinimumContentsLength(8);
      field->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
      field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    connect(source_, &QComboBox::currentIndexChanged, this, [this](int index) {
      if (index < 0)
        return;
      auto ref = reference_;
      ref.source = QUuid(source_->currentData().toString());
      if (ref.source != reference_.source) {
        ref.component = 0;
        if (channel_->currentIndex() < 0 && channel_->count())
          ref.channel = channel_->itemData(0).toInt();
      }
      if (changed)
        changed(ref);
    });
    connect(channel_, &QComboBox::currentIndexChanged, this, [this](int index) {
      if (index >= 0) {
        auto ref = reference_;
        ref.channel = channel_->currentData().toInt();
        if (changed)
          changed(ref);
      }
    });
    connect(component_, &QComboBox::currentIndexChanged, this, [this](int index) {
      if (index >= 0) {
        auto ref = reference_;
        ref.component = index;
        if (changed)
          changed(ref);
      }
    });
    connect(coverage_, &QCheckBox::toggled, this, [this](bool value) {
      auto ref = reference_;
      ref.coverage = value;
      if (changed)
        changed(ref);
    });
    connect(jump_, &QPushButton::clicked, this, [this] {
      if (source_requested)
        source_requested(reference_.source);
    });
  }
  std::function<void(const AnchorReference &)> changed;
  std::function<void(const QUuid &)> source_requested;
  void synchronize(const TextureSet &set,
                   const QUuid &consumer,
                   const AnchorReference &ref,
                   int role)
  {
    reference_ = ref;
    const QSignalBlocker a(source_), b(channel_), c(component_), d(coverage_);
    source_->clear();
    source_->addItem(QString("Baked %1").arg(mesh_map_names[role]), QString());
    const auto &layers = evaluation_layers(set);
    const auto target = std::find_if(
        layers.begin(), layers.end(), [&](const auto &layer) { return layer.id == consumer; });
    for (const auto &point : TextureDocument::mask_anchors(set)) {
      const auto producer = std::find_if(layers.begin(), layers.end(), [&](const auto &layer) {
        return layer.id == point.layer;
      });
      if (point.enabled && producer < target) {
        source_->addItem("Anchor: " + point.name, point.id.toString());
        auto candidate = ref;
        candidate.source = point.id;
        const auto reason = set.selected_mask ?
                                TextureDocument::mask_effect_anchor_reference_problem(
                                    set, consumer, set.selected_effect, candidate) :
                                TextureDocument::anchor_reference_problem(
                                    set, consumer, candidate);
        if (!reason.isEmpty()) {
          const auto index = source_->count() - 1;
          source_->setItemData(index, reason, Qt::ToolTipRole);
          if (auto *model = qobject_cast<QStandardItemModel *>(source_->model()))
            model->item(index)->setEnabled(false);
        }
      }
    }
    int selected = source_->findData(ref.source.isNull() ? QString() : ref.source.toString());
    if (selected < 0) {
      source_->addItem("Unavailable anchor", ref.source.toString());
      selected = source_->count() - 1;
    }
    source_->setCurrentIndex(selected);
    channel_->clear();
    for (int index = 0; index < 5; ++index)
      if (index != 3 && set.channels[index])
        channel_->addItem(channel_names[index], index);
    if (set.ao_channel)
      channel_->addItem("Ambient occlusion", 5);
    for (int slot = 0; slot < 8; ++slot)
      if (set.users[slot].present)
        channel_->addItem(QString::fromStdString(set.users[slot].name), slot + 6);
    if (set.displacement.channel)
      channel_->addItem("Displacement", 14);
    channel_->setCurrentIndex(channel_->findData(ref.channel));
    component_->setCurrentIndex(ref.component);
    coverage_->setChecked(ref.coverage);
    const auto point = TextureDocument::anchor_point(set, ref.source);
    const bool extracts = !ref.source.isNull() && (!point || !point->mask);
    auto *form = static_cast<QFormLayout *>(layout());
    for (auto *field : {channel_, component_}) {
      field->setVisible(extracts);
      form->labelForField(field)->setVisible(extracts);
    }
    coverage_->setVisible(extracts);
    jump_->setVisible(!ref.source.isNull());
    jump_->setEnabled(point.has_value());
    const auto problem = ref.source.isNull() ?
                             QString() :
                         set.selected_mask ?
                             TextureDocument::mask_effect_anchor_reference_problem(
                                 set, consumer, set.selected_effect, ref) :
                             TextureDocument::anchor_reference_problem(set, consumer, ref);
    warning_->setText(problem);
    warning_->setVisible(!problem.isEmpty());
  }

 private:
  AnchorReference reference_;
  QComboBox *source_, *channel_, *component_;
  QCheckBox *coverage_;
  QPushButton *jump_;
  QLabel *warning_;
};
}  // namespace bts
