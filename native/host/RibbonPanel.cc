#include "ResourceBudget.hh"
#include "NumericLocale.hh"
#include "RibbonPanel.hh"
#include "FillTextureIO.hh"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include "FileDialogs.hh"
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
namespace bts {
bool edit_ribbon_settings(QWidget *parent,RibbonSettings &settings){
 QDialog dialog(parent);dialog.setObjectName("ribbonSettingsDialog");dialog.setWindowTitle("Path — Ribbon settings");dialog.resize(460,640);auto *outer=new QVBoxLayout(&dialog);auto *scroll=new QScrollArea(&dialog);scroll->setWidgetResizable(true);auto *body=new QWidget;scroll->setWidget(body);outer->addWidget(scroll);auto *form=new QFormLayout(body);
 auto image=settings.image;auto *enabled=new QCheckBox("Ribbon (continuous image along path)",body);enabled->setObjectName("ribbonEnabled");form->addRow(enabled);
 auto *image_name=new QLabel(body);image_name->setObjectName("ribbonImageName");form->addRow("Image",image_name);auto *load=new QPushButton("Choose image…",body);load->setObjectName("ribbonChooseImage");form->addRow(load);auto *error=new QLabel(body);error->setObjectName("ribbonError");error->setWordWrap(true);form->addRow(error);
 const auto combo=[&](const char *name,const char *label,QStringList items){auto *c=new QComboBox(body);c->setObjectName(name);c->addItems(items);form->addRow(label,c);return c;};
 auto *orientation=combo("ribbonOrientation","Image orientation",{"0°","90°","180°","270°"});
 auto *corner=combo("ribbonCorner","Corner",{"Miter","Round","Bevel","Cut"});
 auto *stretch=combo("ribbonStretch","Stretching",{"Stretch along path","Keep aspect ratio"});
 auto *ratio=combo("ribbonRatio","Aspect mode",{"Fit to path width","Fit to path length"});
 auto *tiling=combo("ribbonTiling","Tiling",{"None","Auto","Custom"});
 auto *count=new QSpinBox(body);count->setObjectName("ribbonCount");count->setRange(1,256);form->addRow("Custom tile count",count);
 auto *alignment=combo("ribbonAlignment","Alignment",{"Start","Center","End"});
 auto *alpha=combo("ribbonAlpha","Self-overlap alpha",{"Normal","Lighten (Max)","Linear dodge"});
 const auto check=[&](const char *name,const char *text){auto *c=new QCheckBox(text,body);c->setObjectName(name);form->addRow(c);return c;};
 auto *flip=check("ribbonFlip","Flip image");auto *mirror=check("ribbonMirror","Mirror alternate tiles");auto *omit=check("ribbonOmitEnds","Omit end sections when closed");auto *offsets=check("ribbonPreserveEnds","Preserve endpoint sections");auto *clip=check("ribbonRemoveClipped","Remove clipped tiles");
 const auto number=[&](const char *name,const char *label,double low,double high){auto *c=new bts::NumericSpinBox(body);c->setObjectName(name);c->setLocale(QLocale::c());c->setDecimals(3);c->setRange(low,high);c->setSingleStep(.05);c->setKeyboardTracking(false);form->addRow(label,c);return c;};
 auto *aspect=number("ribbonAspect","Aspect factor",.01,100);auto *start=number("ribbonStartOffset","Start offset (image fraction)",0,.99);auto *end=number("ribbonEndOffset","End offset (image fraction)",0,.99);
 auto *normal=combo("ribbonNormalOverlap","Self-overlap normal",{"Normal map combine"});normal->setEnabled(false);normal->setToolTip("Requires a nonuniform normal resource; the current Normal input is uniform.");auto *height=combo("ribbonBumpOverlap","Self-overlap bump",{"Lighten (Max)"});height->setEnabled(false);height->setToolTip("Requires a nonuniform bump resource; the current Bump input is uniform.");
 auto *hint=new QLabel("Width and stroke opacity use the brush toolbar. RGBA images are stored scene-linear in the project (loading is budgeted against available RAM/VRAM). Curves remain editable with the Path tool.",body);hint->setWordWrap(true);form->addRow(hint);
 const auto sync=[&](const RibbonSettings &s){image=s.image;image_name->setText(QString::fromStdString(image->name));enabled->setChecked(s.enabled);orientation->setCurrentIndex(s.orientation);corner->setCurrentIndex(int(s.corner));stretch->setCurrentIndex(int(s.stretch));ratio->setCurrentIndex(int(s.ratio));tiling->setCurrentIndex(int(s.tiling));count->setValue(s.count);alignment->setCurrentIndex(int(s.alignment));alpha->setCurrentIndex(int(s.alpha));flip->setChecked(s.flip);mirror->setChecked(s.mirror);omit->setChecked(s.omit_ends);offsets->setChecked(s.offsets_only);clip->setChecked(s.remove_clipped);aspect->setValue(s.aspect);start->setValue(s.start_offset);end->setValue(s.end_offset);};
 const auto availability=[&]{const bool ribbon=enabled->isChecked(),keep=stretch->currentIndex()==1;for(auto *widget:std::initializer_list<QWidget *>{load,orientation,corner,stretch,tiling,alpha,flip,mirror,omit,offsets,aspect})widget->setEnabled(ribbon);ratio->setEnabled(ribbon&&keep);alignment->setEnabled(ribbon&&keep);count->setEnabled(ribbon&&tiling->currentIndex()==2);clip->setEnabled(ribbon&&keep&&ratio->currentIndex()==0);start->setEnabled(ribbon&&offsets->isChecked());end->setEnabled(ribbon&&offsets->isChecked());};
 for(auto *c:{stretch,ratio,tiling})QObject::connect(c,&QComboBox::currentIndexChanged,&dialog,[&]{availability();});for(auto *c:{enabled,offsets})QObject::connect(c,&QCheckBox::toggled,&dialog,[&]{availability();});sync(settings);availability();
 QObject::connect(load,&QPushButton::clicked,&dialog,[&]{bts::FileDialog picker(&dialog,"Choose ribbon image",{},"Images (*.png *.jpg *.jpeg *.tif *.tiff *.exr *.hdr *.tga *.bmp)");picker.setObjectName("ribbonFilePicker");picker.setFileMode(QFileDialog::ExistingFile);picker.setOption(QFileDialog::DontUseNativeDialog);if(picker.exec()!=QDialog::Accepted||picker.selectedFiles().isEmpty())return;try{const QFileInfo info(picker.selectedFiles().front());if(info.size()<1)throw std::invalid_argument("Choose a readable nonempty image");QFile file(info.absoluteFilePath());if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read ribbon image");StudioAsset asset;asset.name=info.completeBaseName();asset.filename=info.fileName();asset.bytes=file.readAll();asset.usages={"texture"};const auto decoded=decode_fill_texture(asset);auto result=std::make_shared<RibbonImage>();result->width=decoded->width;result->height=decoded->height;result->name=decoded->name;result->pixels=*decoded->pixels;result->validate();image=result;image_name->setText(QString::fromStdString(image->name));error->clear();}catch(const std::exception &e){error->setText(QString::fromUtf8(e.what()));}});
 auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel|QDialogButtonBox::RestoreDefaults,&dialog);buttons->button(QDialogButtonBox::Ok)->setObjectName("commitRibbonSettings");buttons->button(QDialogButtonBox::RestoreDefaults)->setObjectName("restoreRibbonSettings");outer->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);QObject::connect(buttons->button(QDialogButtonBox::RestoreDefaults),&QPushButton::clicked,&dialog,[&]{RibbonSettings defaults;defaults.enabled=enabled->isChecked();sync(defaults);availability();});QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{try{RibbonSettings s;s.image=image;s.enabled=enabled->isChecked();s.orientation=orientation->currentIndex();s.corner=RibbonCorner(corner->currentIndex());s.stretch=RibbonStretch(stretch->currentIndex());s.ratio=RibbonRatio(ratio->currentIndex());s.tiling=RibbonTiling(tiling->currentIndex());s.count=count->value();s.alignment=RibbonAlignment(alignment->currentIndex());s.alpha=RibbonAlpha(alpha->currentIndex());s.flip=flip->isChecked();s.mirror=mirror->isChecked();s.omit_ends=omit->isChecked();s.offsets_only=offsets->isChecked();s.remove_clipped=clip->isChecked();s.aspect=aspect->value();s.start_offset=start->value();s.end_offset=end->value();s.validate();settings=s;dialog.accept();}catch(const std::exception &e){error->setText(QString::fromUtf8(e.what()));}});return dialog.exec()==QDialog::Accepted;
}
}
