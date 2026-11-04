#include "ResourceAvailability.hh"
#include "ResourceMonitor.hh"
#include "Performance.hh"
#include "FillTextureIO.hh"
#include "BitmapMask.hh"
#include <QApplication>
#include <QAction>
#include <QEvent>
#include <QLabel>
#include <QLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QSettings>
#include <QSpinBox>
#include <QToolButton>
#include <QWidgetAction>
#include <QVariant>
namespace bts {namespace {
QSettings preferences(){return QSettings("mPainter","ResourceMonitoring");}
bool enabled(const char *key,bool fallback=false){return preferences().value(key,fallback).toBool();}
std::uint64_t decoded_bytes(const FillTexture &texture){if(!texture.tiles)return std::uint64_t(texture.width)*texture.height*sizeof(FillTexture::Pixel);std::uint64_t total=0;for(const auto &[tile,image]:*texture.tiles)total+=std::uint64_t(image->width)*image->height*sizeof(FillTexture::Pixel);return total;}
}
ResourceMonitor::ResourceMonitor(QWidget *owner,ResourceMonitorHooks hooks):QObject(owner),hooks_(std::move(hooks)){
 setObjectName("resourceMonitor");auto *shelf=owner->findChild<QWidget *>("assetShelf");
 auto *bar=new QWidget(shelf);auto *layout=new QHBoxLayout(bar);layout->setContentsMargins(0,0,4,2);layout->addStretch();
 auto *button=new QToolButton(bar);button->setObjectName("resourceUpdateMenuButton");button->setText("⇄");button->setToolTip("Resource updates");button->setPopupMode(QToolButton::InstantPopup);layout->addWidget(button);shelf->layout()->addWidget(bar);
 auto *menu=new QMenu(button);button->setMenu(menu);
 const auto toggle=[&](const char *name,const char *label,const char *key,bool fallback){auto *action=menu->addAction(label);action->setObjectName(name);action->setCheckable(true);action->setChecked(enabled(key,fallback));connect(action,&QAction::toggled,this,[this,key](bool value){auto p=preferences();p.setValue(key,value);p.sync();configure();});};
 toggle("autoUpdateAssets","Assets panel","library",false);toggle("autoUpdateProject","Resources used in project","project",false);
 toggle("resourceUpdateOnFocus","Refresh when application regains focus","focus",true);
 toggle("skipResourceMismatch","Skip assets when parameters mismatch","skip",true);
 auto *interval=new QWidgetAction(menu);auto *row=new QWidget(menu);auto *line=new QHBoxLayout(row);line->addWidget(new QLabel("Check interval (minutes)",row));auto *spin=new QSpinBox(row);spin->setObjectName("resourceUpdateInterval");spin->setRange(0,1440);spin->setValue(std::clamp(preferences().value("minutes",1).toInt(),0,1440));spin->setToolTip("Zero checks every five seconds.");line->addWidget(spin);interval->setDefaultWidget(row);menu->addAction(interval);connect(spin,&QSpinBox::valueChanged,this,[this](int minutes){auto p=preferences();p.setValue("minutes",minutes);p.sync();configure();});
 menu->addSeparator();auto *library=menu->addAction("Refresh assets now");library->setObjectName("refreshAssetsNow");connect(library,&QAction::triggered,this,[this]{refresh(true,false);});
 auto *project=menu->addAction("Update project resources now");project->setObjectName("refreshProjectResourcesNow");connect(project,&QAction::triggered,this,[this]{refresh(false,true);});
 connect(menu,&QMenu::aboutToShow,this,[this,project]{project->setEnabled(!hooks_.project().isEmpty());});
 connect(&timer_,&QTimer::timeout,this,[this]{refresh(enabled("library"),enabled("project"));});poll_.setInterval(30);connect(&poll_,&QTimer::timeout,this,[this]{poll();});qApp->installEventFilter(this);configure();
}
ResourceMonitor::~ResourceMonitor(){qApp->removeEventFilter(this);if(cancel_)cancel_->store(true);if(job_.valid())job_.wait();}
void ResourceMonitor::configure(){timer_.stop();if(enabled("library")||enabled("project")){const int minutes=std::clamp(preferences().value("minutes",1).toInt(),0,1440);timer_.start(minutes?minutes*60000:5000);}}
bool ResourceMonitor::eventFilter(QObject *object,QEvent *event){if(event->type()==QEvent::ApplicationStateChange&&qApp->applicationState()==Qt::ApplicationActive&&enabled("focus"))refresh(enabled("library"),enabled("project"));return QObject::eventFilter(object,event);}
void ResourceMonitor::refresh(bool library,bool project){
 if(!library&&!project)return;
 if(job_.valid()){pending_library_|=library;pending_project_|=project;return;}
 const auto assets=hooks_.sources(library,project);if(assets.empty())return;const QString identity=hooks_.project();const bool skip=enabled("skip",true);cancel_=std::make_shared<std::atomic<bool>>(false);auto cancel=cancel_;
 setProperty("pending",true);job_=std::async(std::launch::async,[assets,identity,skip,cancel]{performance::Scope timing("resource_worker_read_decode");Batch batch;batch.project=identity;auto scan=scan_resources(assets,*cancel);batch.messages=std::move(scan.messages);std::uint64_t decoded=0;
  for(auto &change:scan.updates){if(cancel->load())break;const auto label=change.previous.name;try{
   if(skip&&is_image_sequence(change.previous.bytes)&&is_image_sequence(change.current.bytes)){const auto before=read_image_sequence(change.previous.bytes),after=read_image_sequence(change.current.bytes);bool same=before.size()==after.size();for(std::size_t i=0;same&&i<before.size();++i)same=before[i].tile==after[i].tile;if(!same)throw std::runtime_error("Skipped: UV tile parameters mismatch (disable Skip to accept)");}
   std::uint64_t allocation=0;PreparedResourceChange ready;ready.change=std::move(change);auto &asset=ready.change.current;
   // Project replacements get a new immutable identity. Old packed bytes remain
   // available to existing history entries and to Save immediately after Undo.
   if(asset.scope==AssetScope::Project)asset.id=QUuid::createUuid();
   if(asset.usages.contains("texture")){ready.color=decode_fill_texture(asset);allocation=2*decoded_bytes(*ready.color);ready.raw=decode_fill_texture(asset,true);}
   else if(asset.usages.contains("alpha")||asset.usages.contains("environment")){auto bitmap=read_bitmap_alpha_bytes(asset.bytes,asset.name);allocation=std::uint64_t(bitmap->pixels.size())*sizeof(float);if(asset.usages.contains("alpha")){BrushAlpha alpha;alpha.kind=AlphaKind::Bitmap;alpha.bitmap=bitmap;asset.alpha=alpha;}}
   else throw std::runtime_error("External updates currently support image resources only");
   decoded+=allocation;batch.updates.push_back(std::move(ready));
  }catch(const std::exception &e){batch.messages.append(label+": "+QString::fromUtf8(e.what())+"; retained imported version.");}}
  return batch;
 });poll_.start();
}
void ResourceMonitor::poll(){
 if(!job_.valid()||hooks_.busy()||job_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
 try{auto batch=job_.get();setProperty("messages",batch.messages);for(const auto &message:batch.messages)hooks_.log(message);
  for(const auto &update:batch.updates){if(update.change.previous.scope==AssetScope::Project&&batch.project!=hooks_.project())continue;try{performance::Scope timing("resource_owner_install");hooks_.apply(update);}catch(const std::exception &e){hooks_.log(QString::fromUtf8(e.what()));}}
 }catch(const std::exception &e){hooks_.log(QString::fromUtf8(e.what()));}
 setProperty("pending",false);setProperty("completed",property("completed").toInt()+1);poll_.stop();const bool library=pending_library_,project=pending_project_;pending_library_=pending_project_=false;if(library||project)refresh(library,project);
}
}
