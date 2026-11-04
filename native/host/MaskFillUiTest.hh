#pragma once
#include "MaskFillPanel.hh"
#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QToolButton>
namespace bts {
inline void verify_shared_mask_fill_ui(QWidget &shell,MaskFillPanel &panel,TextureSet &set,const QString &directory,const std::function<void()> &show,const std::function<void(QString)> &capture){
 const auto require=[](bool okay,const char *why){if(!okay)qFatal("AO shared mask fill: %s",why);};
 const QImage fill_image(directory+"/fill-properties.png");MaskEffect effect;effect.kind=MaskEffectKind::Fill;effect.fill=.5f;set.layers.back().mask=LayerMask{};set.layers.back().mask->effects={effect};set.selected_mask=true;set.selected_effect=effect.id;
 panel.changed=[&](const MaskEffect &value){effect=value;set.layers.back().mask->effects={effect};show();};show();
 const auto get=[&]<class T>(const char *name){auto *w=panel.findChild<T *>(name);require(w,name);return w;};
 for(const char *name:{"maskFillProjection","maskFillFiltering","maskFillWrap","maskFillScale0","maskFillScale1","maskFillRotation","maskFillOffset0","maskFillOffset1","maskSpatial_fillScaleMode","maskSpatial_fillPhysicalSizeX","maskSpatial_fill3dDisclosure","maskSpatial_fillWarpDisclosure","maskSpatial_fillSymmetryPanel","maskSpatial_fillShowManipulator"})get.operator()<QWidget>(name);
 int buttons=0,headers=0;for(auto *w:panel.findChildren<QCheckBox *>())if(w->property("fillChannelToggle").toBool()&&w->isVisibleTo(&panel))++buttons;for(auto *w:panel.findChildren<QToolButton *>())if(w->property("fillChannelHeader").toBool()&&w->isVisibleTo(&panel))++headers;require(buttons==0&&headers==1,"single grayscale row without MATERIAL buttons");
 auto *gradient=get.operator()<QWidget>("maskFillGradient");require(gradient->isVisibleTo(&panel),"uniform gradient visible");int starts=0,ends=0;panel.edit_started=[&]{++starts;};panel.edit_finished=[&]{++ends;};
 const auto mouse=[&](QEvent::Type t,double value){const QPointF point(7+value*std::max(1,gradient->width()-14),25);QMouseEvent e(t,point,gradient->mapToGlobal(point.toPoint()),t==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,t==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(gradient,&e);};mouse(QEvent::MouseButtonPress,.2);mouse(QEvent::MouseMove,.75);mouse(QEvent::MouseButtonRelease,.75);require(std::abs(effect.fill-.75)<.01&&starts==1&&ends==1,"shared gradient drag and boundaries");
 capture(directory+"/mask-fill-shared.png");QImage mask_image(directory+"/mask-fill-shared.png");QImage pair(fill_image.width()+mask_image.width()+8,std::max(fill_image.height(),mask_image.height()),QImage::Format_RGB32);pair.fill(QColor("#333333"));{QPainter painter(&pair);painter.drawImage(0,0,fill_image);painter.drawImage(fill_image.width()+8,0,mask_image);}require(pair.save(directory+"/fill-mask-side-by-side.png"),"side by side capture");
 get.operator()<QComboBox>("maskFillResource")->setCurrentIndex(1);
 const auto samples=[&](const FillSurfaceAtlas *atlas=nullptr){std::vector<float> result;for(int y=3;y<64;y+=5)for(int x=3;x<64;x+=5)result.push_back(mask_fill_sample(effect.mapping,effect.fill,x,y,64,64,1001,atlas));return result;};
 const auto initial=samples();get.operator()<QDoubleSpinBox>("maskFillScale0")->setValue(2);const auto scaled=samples();require(initial!=scaled,"scale changes mask samples");get.operator()<QDoubleSpinBox>("maskFillRotation")->setValue(31);const auto rotated=samples();require(scaled!=rotated,"rotation changes mask samples");
 effect.mapping.wrap=1;set.layers.back().mask->effects={effect};show();get.operator()<QDoubleSpinBox>("maskFillOffset0")->setValue(.17);require(effect.mapping.wrap==1,"legacy clamp survives transform edit");get.operator()<QComboBox>("maskFillWrap")->setCurrentIndex(2);require(effect.mapping.wrap==3&&effect.mapping.projection.wrap==2,"horizontal wrap mapping");require(read_mask_fill_mapping(mask_fill_mapping_json(effect.mapping)).wrap==3,"wrap serialization");get.operator()<QComboBox>("maskFillWrap")->setCurrentIndex(0);
 FillSurfaceAtlas atlas;atlas.resolution=16;atlas.extent=2;auto &points=atlas.tiles[1001];points.resize(256);for(int y=0;y<16;++y)for(int x=0;x<16;++x){auto &p=points[y*16+x];p.valid=true;p.position={x/16.f-.37f,y/16.f-.21f,(x+y)/32.f};p.normal={.3f,.5f,.8f};p.footprint=1.f/16;}
 const auto uv=samples(&atlas);get.operator()<QComboBox>("maskFillProjection")->setCurrentIndex(1);require(samples(&atlas)!=uv,"triplanar changes mask samples");require(get.operator()<QWidget>("maskSpatial_fillTriplanarHardness")->isVisibleTo(&panel),"triplanar hardness visible");capture(directory+"/mask-fill-triplanar.png");
 require(read_mask_fill_mapping(mask_fill_mapping_json(effect.mapping)).projection.mode==1,"mapping roundtrip");
 QFile file(directory+"/mask-fill-ui.json");require(file.open(QIODevice::WriteOnly),"proof");file.write(QJsonDocument(QJsonObject{{"single_grayscale_row",headers},{"visible_material_buttons",buttons},{"gradient_value",.75},{"drag_started",starts},{"drag_finished",ends},{"scale_changes_samples",true},{"rotation_changes_samples",true},{"triplanar_changes_samples",true},{"legacy_clamp_retained",true},{"new_wrap_roundtrip",3}}).toJson());
}
}
