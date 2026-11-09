// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.hh"
#pragma once
#include "TextureDocument.hh"
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QTimer>
#include <functional>
namespace bts {
class LayerInstancePanel final : public QWidget {
 public:
  explicit LayerInstancePanel(QWidget *parent) : QWidget(parent)
  {
    setObjectName("layerInstanceProperties");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel("LAYER INSTANCE HIERARCHY", this));
    tree_ = new QTreeWidget(this);
    tree_->setObjectName("layerInstanceHierarchy");
    tree_->setHeaderHidden(true);
    tree_->setMinimumHeight(140);
    tree_->setMaximumHeight(240);
    layout->addWidget(tree_);
    warning_ = new QLabel(this);
    warning_->setObjectName("layerInstanceWarning");
    warning_->setWordWrap(true);
    warning_->setStyleSheet(bts::theme::qss("color: @Warning@"));
    layout->addWidget(warning_);
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
      if (!item)
        return;
      const QUuid set(item->data(0, Qt::UserRole).toString()),
          layer(item->data(0, Qt::UserRole + 1).toString());
      QTimer::singleShot(0, this, [this, set, layer] {
        if (activated)
          activated(set, layer);
      });
    });
  }
  std::function<void(QUuid, QUuid)> activated;
  void synchronize(const TextureSet *set, const TextureLayer *layer, bool active)
  {
    setVisible(active && set && layer);
    if (!active || !set || !layer)
      return;
    tree_->clear();
    std::vector<QTreeWidgetItem *> parents;
    const auto found = set->instance_hierarchies.find(layer->id);
    if (found != set->instance_hierarchies.end())
      for (const auto &node : found->second) {
        auto *item = node.depth > 0 && int(parents.size()) >= node.depth ?
                         new QTreeWidgetItem(parents[node.depth - 1], {node.label}) :
                         new QTreeWidgetItem(tree_, {node.label});
        item->setData(0, Qt::UserRole, node.set.toString());
        item->setData(0, Qt::UserRole + 1, node.layer.toString());
        if (node.set == set->id && node.layer == layer->id)
          tree_->setCurrentItem(item);
        if (int(parents.size()) <= node.depth)
          parents.resize(node.depth + 1);
        parents[node.depth] = item;
      }
    tree_->expandAll();
    const auto problem = set->instance_problems.find(layer->id);
    warning_->setText(problem == set->instance_problems.end() ? QString{} : problem->second);
    warning_->setVisible(problem != set->instance_problems.end());
  }

 private:
  QTreeWidget *tree_;
  QLabel *warning_;
};
}  // namespace bts
