#pragma once
// A titled group inside the Properties panel: a heading that folds the group
// away, the controls under it, and the spacing the interface guide gives a
// section. It hides itself whenever everything inside it is hidden, so a
// heading never stands over empty space — docs/design/ui.md §7.2.
#include "Theme.hh"
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
namespace bts {
class PropertySection final : public QWidget {
 public:
  explicit PropertySection(const QString &title,QWidget *parent=nullptr,bool folded_by_default=false)
      :QWidget(parent),key_("properties/section/"+QString(title).remove(' ').toLower())
  {
    setObjectName("propertySection"+QString(title).remove(' '));
    auto *layout=new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(theme::SpaceWide);
    heading_=new QToolButton(this);
    heading_->setObjectName("propertySectionTitle");
    heading_->setText(title.toUpper());
    heading_->setCheckable(true);
    heading_->setAutoRaise(true);
    heading_->setFocusPolicy(Qt::StrongFocus);
    heading_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    heading_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    heading_->setToolTip("Fold this section away");
    layout->addWidget(heading_);
    content_=new QWidget(this);
    content_layout_=new QVBoxLayout(content_);
    content_layout_->setContentsMargins(0,0,0,0);
    content_layout_->setSpacing(theme::Space);
    layout->addWidget(content_);
    // Folding is only a convenience if it is remembered. A section that springs
    // back open on every selection is an extra click, not a simplification.
    heading_->setChecked(QSettings().value(key_,folded_by_default).toBool());
    connect(heading_,&QToolButton::toggled,this,[this](bool folded){
      QSettings().setValue(key_,folded);
      apply_fold();
    });
    apply_fold();
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
  bool folded()const{return heading_->isChecked();}

 private:
  void apply_fold()
  {
    const bool folded=heading_->isChecked();
    heading_->setArrowType(folded?Qt::RightArrow:Qt::DownArrow);
    content_->setVisible(!folded);
  }
  QToolButton *heading_=nullptr;QWidget *content_=nullptr;QVBoxLayout *content_layout_=nullptr;
  QString key_;
};
}
