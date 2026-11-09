// SPDX-License-Identifier: GPL-3.0-or-later
#include "OrderedRawRgbFixture.hh"
#include "OrdinaryColorSource.hh"
#include "TextureLoadController.hh"
#include "LayerApi.hh"
#include "LayerChannelCommand.hh"
#include "BlenderHost.hh"
#include "SourceCpuRegression.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QThread>

using namespace bts;
using ordered_rgb_test::require;
namespace {
void wait(const auto &ready){QElapsedTimer clock;clock.start();while(!ready()){QCoreApplication::processEvents();QThread::msleep(1);require(clock.elapsed()<15000,"Ordinary Color source test timed out");}}
bool rejects(const auto &work){try{work();}catch(const std::invalid_argument &error){require(QString::fromUtf8(error.what()).contains("Remove the Linear RGB interpretation"),"Unexpected source rejection");return true;}return false;}
void api(){
 auto document=ordered_rgb_test::document();const auto before=document.to_json();const auto pixels=document.set(1).layers.back().fill.texture->pixels;
 const auto reference=layer_api_reference(document,document.set(1),document.set(1).layers.back());
 int reads=0;LayerApiResources resources;StudioAsset asset;asset.usages={"texture"};
 resources.asset=[&](const QUuid &,bool){++reads;return asset;};resources.texture=[&](const StudioAsset &,bool){++reads;return ordered_rgb_test::texture();};
 for(const QJsonValue &source:{QJsonValue(asset.id.toString()),QJsonValue(QJsonArray{1,.5,.25}),QJsonValue(QJsonObject{{"type","atom"}})}){
  require(rejects([&]{layer_api_edit(document,"layers.set_source",{{"reference",reference},{"channel",0},{"source",source}},resources);}),"Marked ordinary API source was accepted");
  require(document.to_json()==before&&document.set(1).layers.back().fill.texture->pixels==pixels&&reads==0,"Rejected source performed IO or changed document");
 }
 document.set(1).layers.back().effects.back().enabled=false;
 require(rejects([&]{require_ordinary_color_source_replacement(document.set(1).layers.back(),0);}),"Disabled interpretation allowed source replacement");
 document.set(1).layers.back().values.base_color[0]=.3f;require(raw_linear_color_source(document.set(1).layers.back()),"Tint invalidated interpretation");
 layer_api_edit(document,"layers.set_projection",{{"reference",reference},{"parameters",QJsonObject{{"linked",false},{"scale",QJsonArray{2,3}}}}},{});
 layer_api_edit(document,"layers.set_source",{{"reference",reference},{"channel",2},{"source",.7}},{});
 require(raw_linear_color_source(document.set(1).layers.back()),"Mapping or other channel edit lost interpretation");
 auto undo=document;set_layer_channel(document.set(1),document.set(1).layers.back().id,0,false);
 require(!raw_linear_color_source(document.set(1).layers.back())&&!document.set(1).layers.back().fill.texture,"Color OFF retained raw boundary");
 require_ordinary_color_source_replacement(document.set(1).layers.back(),0);document=undo;
 require(raw_linear_color_source(document.set(1).layers.back()),"Undo lost interpretation");
}
void queued(const StudioAsset &asset,bool expected_raw){
 auto document=ordered_rgb_test::document();auto &layer=document.set(1).layers.back();const auto boundary=layer.effects.back();layer.effects.pop_back();
 int published_history=0,prepared=0,released=0,installed=0,errors=0;bool allow_install=false;QString error;std::shared_ptr<const FillTexture> decoded;
 TextureLoadTarget target;target.document=document.id();target.set=document.set(1).id;target.layer=layer.id;target.channel=0;
 TextureLoadHooks hooks;hooks.valid=[](const auto &){return true;};hooks.busy=[]{return false;};hooks.status=[](const auto &,const auto &){};
 hooks.prepare=[&](const LoadedTexture &loaded){++prepared;decoded=loaded.texture;require(decoded->raw==expected_raw,"Ordinary decoded role mismatch");return allow_install;};
 hooks.release_preparation=[&]{++released;};hooks.error=[&](const QString &message){++errors;error=message;};
 hooks.install=[&](LoadedTexture loaded){++installed;auto candidate=document;auto &fill=candidate.set(1).layers.back();require_ordinary_color_source_replacement(fill,loaded.target.channel);fill.fill.texture=loaded.texture;document=std::move(candidate);++published_history;};
 TextureLoadController loader(nullptr,std::move(hooks));
 // Request checks precede controller enqueue, so rejected choices do no IO.
 document.set(1).layers.back().effects.push_back(boundary);
 const auto marked=document.to_json();require(rejects([&]{require_ordinary_color_source_replacement(document.set(1).layers.back(),0);loader.from_asset(target,asset);}),"Request accepted marked Color");
 require(!loader.pending()&&prepared==0&&installed==0&&released==0,"Rejected request started work");
 document.set(1).layers.back().effects.pop_back();require_ordinary_color_source_replacement(document.set(1).layers.back(),0);loader.from_asset(target,asset);
 wait([&]{return prepared>0||errors;});require(decoded&&errors==0&&loader.pending(),"Decode did not reach owner preparation barrier");
 document.set(1).layers.back().effects.push_back(boundary);const auto source=document.set(1).layers.back().fill.texture;require(document.to_json()==marked,"Barrier fixture changed document");allow_install=true;
 wait([&]{return !loader.pending();});
 require(errors==1&&error.contains("Remove the Linear RGB interpretation")&&installed==1&&released==1,"Queued install did not reject and release preparation exactly once");
 require(document.to_json()==marked&&document.set(1).layers.back().fill.texture==source&&published_history==0,"Queued rejection changed source/document/history");
 std::cout<<"ORDINARY_COLOR_REJECT "<<asset.filename.toStdString()<<" decoded_raw="<<expected_raw<<" prepare_calls="<<prepared<<" released="<<released<<std::endl;
}
}
void bts::run_ordinary_color_source_cpu_regression(const QString &output){
 require(QDir().mkpath(output),"Cannot create private Color source regression directory");api();
 QImage image(2,2,QImage::Format_RGBA8888);image.fill(QColor(80,140,210,255));require(image.save(output+"/canonical.png"),"Cannot save synthetic PNG");
 auto *exr=blender::IMB_allocImBuf(2,2,blender::ImBufFlags::FloatData);require(exr,"No EXR buffer");{const auto release=qScopeGuard([&]{blender::IMB_freeImBuf(exr);});exr->channels=4;exr->color_mode=blender::ImColorMode::RGBA;exr->ftype=blender::IMB_FTYPE_OPENEXR;exr->flags|=blender::ImBufFlags::AlphaChannelPacked;for(int i=0;i<4;++i){float *v=exr->float_data_for_write()+4*i;v[0]=.12f;v[1]=.3f;v[2]=1.5f;v[3]=1;}require(blender::IMB_save_image(exr,(output+"/canonical.exr").toUtf8().constData(),blender::ImBufFlags::FloatData),"Cannot save synthetic EXR");}
 for(const QString &name:{QString("canonical.png"),QString("canonical.exr")}){QFile file(output+"/"+name);require(file.open(QIODevice::ReadOnly),"Cannot read synthetic image");StudioAsset asset;asset.name=name;asset.filename=name;asset.bytes=file.readAll();asset.usages={"texture"};asset.scope=AssetScope::Project;queued(asset,false);}
 StudioAsset map;map.name="Canonical raw BTSMAP";map.filename="canonical.btsmap";map.bytes=ordered_rgb_test::encoded();map.usages={"texture"};map.scope=AssetScope::Project;queued(map,true);
 std::cout<<"Ordinary Color source guard PASS: real PNG/EXR/BTSMAP decode, request no-work, queued recheck, unchanged document/history, release, disabled boundary, tint/mapping/Roughness/AG Undo; GPU contexts=0"<<std::endl;
}
#ifndef BTS_EMBED_SOURCE_REGRESSIONS
int main(int argc,char **argv){QApplication app(argc,argv);try{
 require(argc==2,"Expected private output directory");BlenderHost host(QCoreApplication::applicationFilePath().toStdString(),"");const auto close=qScopeGuard([&]{host.shutdown();});run_ordinary_color_source_cpu_regression(QString::fromLocal8Bit(argv[1]));return 0;
}catch(const std::exception &error){std::cerr<<"Ordinary Color source guard FAIL "<<error.what()<<std::endl;return 1;}}
#endif
