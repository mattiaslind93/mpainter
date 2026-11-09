// SPDX-License-Identifier: GPL-3.0-or-later
#include "MaterialSpherePreview.hh"
#include "BuiltinSmartMasks.hh"
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QPainter>
namespace bts {
QString StudioAsset::hash() const {return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
void hydrate_asset_presets(StudioAsset &a){if(a.smart_mask)a.smart_mask=std::make_shared<const LayerMask>(read_mask_preset(a.bytes));}
}
int main(int argc,char **argv){
 QGuiApplication app(argc,argv);using namespace bts;
 const QString out=QString::fromLocal8Bit(argv[1]);QDir().mkpath(out);
 std::vector<StudioAsset> masks;
 for(const auto &asset:builtin_smart_mask_assets())if(asset.smart_mask)masks.push_back(asset);
 for(int kind:{1,2}){LayerMask mask;mask.background=0;MaskEffect e;e.kind=MaskEffectKind::Generator;e.generator=kind;if(kind==2){e.contrast=4;e.bias=-.5f;}mask.effects.push_back(e);StudioAsset a;a.name=kind==1?"Position height":"Curvature edges";a.smart_mask=std::make_shared<const LayerMask>(mask);a.bytes=write_mask_preset(mask);masks.push_back(a);}
 QImage sheet(1120,500,QImage::Format_RGB32);sheet.fill(QColor("#202323"));QPainter painter(&sheet);painter.setPen(Qt::white);QJsonArray records;
 int i=0;for(const auto &a:masks){QFile f(out+QString("/%1.btsmask").arg(i));if(!f.open(QIODevice::WriteOnly))return 2;f.write(a.bytes);auto image=smart_mask_sphere_preview(*a.smart_mask,208);image.save(out+QString("/%1.png").arg(i));painter.drawImage((i%5)*224+8,(i/5)*250,image);painter.drawText(QRect((i%5)*224,(i/5)*250+210,224,32),Qt::AlignCenter,a.name);records.append(QJsonObject{{"name",a.name},{"effects",mask_effects_json(a.smart_mask->effects)}});++i;}
 painter.end();sheet.save(out+"/reference.png");QFile f(out+"/reference.json");if(!f.open(QIODevice::WriteOnly))return 3;f.write(QJsonDocument(records).toJson());
}
