#pragma once
// A titled group inside the Properties panel: an uppercase heading, the
// controls under it, and the spacing the interface guide gives a section.
// It hides itself whenever everything inside it is hidden, so a heading never
// stands over empty space — see docs/design/ui.md §7.2.
#include "Theme.hh"
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
namespace bts {
class PropertySection final : public QWidget {
 public:
  explicit PropertySection(const QString &title,QWidget *parent=nullptr):QWidget(parent)
  {
    setObjectName("propertySection"+QString(title).remove(' '));
    auto *layout=new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(theme::SpaceWide);
    auto *heading=new QLabel(title.toUpper(),this);
    heading->setObjectName("propertySectionTitle");
    layout->addWidget(heading);
    content_=new QWidget(this);
    content_layout_=new QVBoxLayout(content_);
    content_layout_->setContentsMargins(0,0,0,0);
    content_layout_->setSpacing(theme::Space);
    layout->addWidget(content_);
    hide();
  }
  void add(QWidget *widget){content_layout_->addWidget(widget);}
  // Called once the panel has decided what each control should show. isHidden()
  // reports an explicit hide rather than "not on screen", so a section nested in
  // a hidden parent still reads its own contents correctly.
  void sync_visibility()
  {
    for(auto *child:content_->findChildren<QWidget *>(QString(),Qt::FindDirectChildrenOnly))
      if(!child->isHidden()){show();return;}
    hide();
  }
 private:
  QWidget *content_=nullptr;QVBoxLayout *content_layout_=nullptr;
};
}
