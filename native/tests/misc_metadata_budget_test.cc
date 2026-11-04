#include <QtWidgets>
// Exercise the native PreviewImage span helper without exposing a test-only API.
#include "../host/BlendMaterial.cc"
#include "ProjectTemplate.hh"
#include "GeometryMask.hh"
#include "MeshBakeDialog.hh"
#include <QtEndian>
#include <zlib.h>
#include <cassert>
#include <climits>
#include <iostream>
using namespace bts;
namespace {
int queries=0;std::uint64_t small_ram(){++queries;return 16*1024*1024;}
template<class F>bool rejects(F &&f){try{f();}catch(const std::exception &){return true;}return false;}
StudioAsset preview_asset(const QByteArray &png){StudioAsset asset;asset.filename="fixture.btsuniform";asset.bytes=QJsonDocument(QJsonObject{{"type","bts-blend-uniform-material"},{"version",1},{"preview_png",QString::fromLatin1(png.toBase64())}}).toJson(QJsonDocument::Compact);return asset;}
}
int main(int argc,char **argv){
 QApplication app(argc,argv);assert(argc==2);QDir().mkpath(QString::fromLocal8Bit(argv[1]));
 QImage image(2,2,QImage::Format_RGBA8888);image.fill(QColor(23,51,92,255));QByteArray png;QBuffer sink(&png);sink.open(QIODevice::WriteOnly);assert(image.save(&sink,"PNG"));auto asset=preview_asset(png);asset.bytes+=QByteArray(2*1024*1024,' ');const int allocation_limit=QImageReader::allocationLimit();const auto reference=QImage::fromData(png,"PNG"),actual=blend_material_preview(asset);assert(!reference.isNull()&&actual==reference);assert(QImageReader::allocationLimit()==allocation_limit);
 {TextureRamQueryScope low([]()->std::uint64_t{return 0;});assert(blend_material_preview(asset).isNull());}
 auto corrupt=preview_asset("invalid PNG");assert(blend_material_preview(corrupt).isNull());corrupt.filename="unsupported.file";assert(blend_material_preview(corrupt).isNull());
 auto huge=png;qToBigEndian<quint32>(50000,reinterpret_cast<uchar *>(huge.data()+16));qToBigEndian<quint32>(50000,reinterpret_cast<uchar *>(huge.data()+20));qToBigEndian<quint32>(crc32(0,reinterpret_cast<const Bytef *>(huge.constData()+12),17),reinterpret_cast<uchar *>(huge.data()+29));{TextureRamQueryScope ram(small_ram);queries=0;assert(blend_material_preview(preview_asset(huge)).isNull()&&queries>=3);}assert(QImageReader::allocationLimit()==allocation_limit);
 blender::PreviewImage native{};native.w[1]=2049;native.h[1]=32;native.rect[1]=MEM_new_array<unsigned int>(2049*32,"preview fixture");auto *pixels=reinterpret_cast<uchar *>(native.rect[1]);for(int i=0;i<2049*32;++i){pixels[i*4]=23;pixels[i*4+1]=51;pixels[i*4+2]=92;pixels[i*4+3]=255;}
 // Preserve authored pixels at source resolution; only the Shelf view scales.
 const auto thumbnail=preview_pixels(native);const QImage native_reference(pixels,2049,32,2049*4,QImage::Format_RGBA8888);assert(!thumbnail.isNull()&&thumbnail.size()==QSize(2049,32)&&thumbnail==native_reference.mirrored(false,true)&&thumbnail.pixelColor(0,0)==QColor(23,51,92,255));{TextureRamQueryScope low([]()->std::uint64_t{return 0;});assert(preview_pixels(native).isNull());}native.h[1]=2049;assert(preview_pixels(native).isNull());native.w[1]=UINT_MAX;assert(preview_pixels(native).isNull());MEM_delete(native.rect[1]);native.rect[1]=nullptr;
 const auto valid=QString(127,QChar(0x00e9))+"a",invalid=QString(128,QChar(0x00e9));assert(valid.toUtf8().size()==255&&invalid.toUtf8().size()==256);BlendMeshSelection selection;selection.objects={valid.toStdString()};selection.collections={std::string(255,'a')};selection.validate();assert(BlendMeshSelection::from_json(selection.to_json())==selection);selection.objects={invalid.toStdString()};assert(rejects([&]{selection.validate();}));selection.objects={std::string(1,char(0xff))};assert(rejects([&]{selection.validate();}));
 GeometryMask geometry;geometry.set(valid.toStdString(),false);assert(read_geometry_mask(geometry_mask_json(geometry)).meshes==geometry.meshes);geometry.meshes.clear();geometry.set(invalid.toStdString(),false);assert(rejects([&]{read_geometry_mask(geometry_mask_json(geometry));}));
 SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Fixture";material.uniform_editable=true;scene.materials.push_back(material);ImportSummary imported;imported.materials={{1,"Fixture"}};TextureDocument document({},imported,scene);
 const QByteArray hdr="parser fixture";const QString resource_name=QString(512,QChar('r'))+".hdr",transform=QString(5000,QChar('v'));QJsonObject resource{{"name",resource_name},{"data",QString::fromLatin1(hdr.toBase64())},{"sha256",QString::fromLatin1(QCryptographicHash::hash(hdr,QCryptographicHash::Sha256).toHex())}};const auto data=make_project_template(document,1,{{"view_transform",transform}},{{"rotation",0},{"exposure",0},{"opacity",100},{"blur",0},{"alignment","World"}},resource,{});const auto path=QString::fromLocal8Bit(argv[1])+"/large-metadata.btst";write_project_template(path,data);assert(read_project_template(path)==data);{TextureRamQueryScope low([]()->std::uint64_t{return 0;});assert(rejects([&]{validate_project_template(data);}));}
 const QString marker(70000,QChar('m'));apply_mesh_bake_template(document.id(),{{"low_marker",marker},{"high_marker",marker},{"ignore_marker",marker}});bool checked=false;QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());assert(dialog);for(auto name:{"bakeLowSuffix","bakeHighSuffix","bakeIgnoreSuffix"}){auto *field=dialog->findChild<QLineEdit *>(name);assert(field&&field->text()==marker&&field->maxLength()==INT_MAX);}checked=true;dialog->done(2);});assert(!choose_mesh_bake(nullptr,document,scene)&&checked);for(auto key:{"low_marker","high_marker","ignore_marker"})assert(mesh_bake_template(document.id())[key].toString()==marker);
 {TextureRamQueryScope low([]()->std::uint64_t{return 0;});assert(rejects([&]{choose_mesh_bake(nullptr,document,scene);}));}
 std::cout<<"Misc metadata: >1MiB valid preview, actual header/span/RAM guards, >2048 native thumbnail,255 UTF8 name boundary, long template fields and70K markers PASS\n";
}
