#include "Theme.hh"
#include "NumericLocale.hh"
#include "UsdScenePanel.hh"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>
#include <stdexcept>
namespace bts {
UsdScenePanel::UsdScenePanel(QLineEdit *path,const UsdImportOptions &options,QWidget *parent,std::string fallback):QWidget(parent),path_(path),fallback_(std::move(fallback)),options_(options){
  setObjectName("usdScenePanel");auto *form=new QFormLayout(this);form->setContentsMargins(0,0,0,0);auto *row=new QHBoxLayout;scope_=new QLabel(this);scope_->setObjectName("usdScopeSummary");scope_->setWordWrap(true);change_=new QPushButton("Change…",this);change_->setObjectName("usdChangeScope");change_->setAutoDefault(false);row->addWidget(scope_,1);row->addWidget(change_);form->addRow("Scope and variants",row);subdivision_=new QSpinBox(this);subdivision_->setObjectName("usdSubdivision");subdivision_->setRange(0,11);subdivision_->setToolTip("Subdivision is evaluated after UV unwrapping. Geometry explicitly marked none remains unsubdivided.");form->addRow("Subdivision level",subdivision_);frame_=new bts::NumericSpinBox(this);frame_->setObjectName("usdFrame");frame_->setDecimals(3);frame_->setRange(-1000000,1000000);form->addRow("Frame",frame_);error_=new QLabel(this);error_->setObjectName("usdSelectionError");error_->setWordWrap(true);error_->setStyleSheet(bts::theme::qss("color:@Warning@"));form->addRow(error_);
  connect(path_,&QLineEdit::textChanged,this,[this]{const auto value=source();if(value!=last_path_){options_={};last_path_=value;}refresh();});connect(change_,&QPushButton::clicked,this,[this]{choose_scope();});connect(subdivision_,&QSpinBox::valueChanged,this,[this](int value){if(!refreshing_)options_.subdivision=value;});connect(frame_,&QDoubleSpinBox::valueChanged,this,[this](double value){if(!refreshing_){options_.frame=value;options_.explicit_frame=true;}});last_path_=source();refresh();
}
std::string UsdScenePanel::source() const{return path_->text().isEmpty()?fallback_:path_->text().toStdString();}
UsdImportOptions UsdScenePanel::options() const{return options_;}
void UsdScenePanel::set_options(const UsdImportOptions &options){options_=options;refresh();}
void UsdScenePanel::refresh(){
  const auto path=source();setVisible(is_usd_scene(path));if(!is_usd_scene(path))return;refreshing_=true;
  try{info_=inspect_usd_scene(path,options_);scope_->setText(QString::fromStdString(options_.scope=="/"?"Root":options_.scope)+QString(" · %1 mesh(es), %2 variant override(s)").arg(info_.meshes).arg(options_.variants.size()));change_->setEnabled(true);subdivision_->setEnabled(info_.subdividable);if(!info_.subdividable)options_.subdivision=0;subdivision_->setValue(options_.subdivision);frame_->setEnabled(info_.animated);frame_->setValue(options_.explicit_frame?options_.frame:info_.first_frame);error_->clear();}
  catch(const std::exception &failure){scope_->setText("USD selection unavailable");try{info_=inspect_usd_scene(path,{});change_->setEnabled(true);}catch(...){change_->setEnabled(false);}subdivision_->setEnabled(false);frame_->setEnabled(false);error_->setText(QString::fromUtf8(failure.what()));}refreshing_=false;
}
void UsdScenePanel::choose_scope(){
  QDialog dialog(this);dialog.setObjectName("usdScopeDialog");dialog.setWindowTitle("USD scope and variants");dialog.resize(620,440);auto *layout=new QVBoxLayout(&dialog);auto *form=new QFormLayout;layout->addLayout(form);auto candidate=options_;auto *scope=new QComboBox(&dialog);scope->setObjectName("usdScope");for(const auto &path:info_.scopes)scope->addItem(QString::fromStdString(path=="/"?"Root":path),QString::fromStdString(path));int initial=scope->findData(QString::fromStdString(candidate.scope));if(initial<0){candidate={};initial=0;}scope->setCurrentIndex(initial);form->addRow("Modeling scope",scope);auto *variants=new QWidget(&dialog);auto *variant_form=new QFormLayout(variants);layout->addWidget(variants);auto *error=new QLabel(&dialog);error->setWordWrap(true);error->setObjectName("usdScopeError");layout->addWidget(error);layout->addStretch();
  const auto rebuild=[&]{while(variant_form->count()){auto *item=variant_form->takeAt(0);if(item->widget())delete item->widget();delete item;}try{const auto info=inspect_usd_scene(source(),candidate);for(const auto &variant:info.variants){auto *combo=new QComboBox(variants);combo->setObjectName("usdVariant");combo->setProperty("usdVariantKey",QString::fromStdString(variant.key));for(const auto &choice:variant.choices)combo->addItem(QString::fromStdString(choice));combo->setCurrentText(QString::fromStdString(variant.selected));variant_form->addRow(QString::fromStdString(variant.key),combo);connect(combo,&QComboBox::currentTextChanged,&dialog,[&,key=variant.key](const QString &value){candidate.variants[key]=value.toStdString();});}error->clear();}catch(const std::exception &failure){error->setText(QString::fromUtf8(failure.what()));}};
  connect(scope,&QComboBox::currentIndexChanged,&dialog,[&]{candidate.scope=scope->currentData().toString().toStdString();rebuild();});rebuild();auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{try{const auto selected=inspect_usd_scene(source(),candidate);if(!selected.meshes)throw std::invalid_argument("Selected scope contains no polygon meshes");dialog.accept();}catch(const std::exception &failure){error->setText(QString::fromUtf8(failure.what()));}});if(dialog.exec()==QDialog::Accepted){options_=candidate;refresh();}
}
}
