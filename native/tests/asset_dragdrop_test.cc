#include "UserSessionGuard.hh"
#include "AssetApplyProgress.hh"
#include "AssetViewportDrop.hh"
#include "LayerStackList.hh"
#include <QMainWindow>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QLabel>
#include <QDir>
#include <QEventLoop>
#include <cassert>
#include <cstdio>
using namespace bts;
namespace {
void wait(int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
void enter(QWidget *view,QPoint position,const QMimeData &mime){QDragEnterEvent event(position,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&event);assert(event.isAccepted());}
void drop(QWidget *view,QPoint position,const QMimeData &mime){QDropEvent event(position,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&event);assert(event.isAccepted());}
UvFaceTriangle triangle(float x,float z,std::size_t material,std::uint32_t object){UvFaceTriangle t;t.material=material;t.triangle_id=std::uint64_t(object)<<32;t.positions={{{x-1,-1,z},{x+1,-1,z},{x,1,z}}};return t;}
}
int main(int argc,char **argv){
 QApplication app(argc,argv);app.setStyle("Fusion");
 assert(cpu_test_arguments({"app","--cpu-asset-dragdrop-test","/tmp/proof"}));assert(!cpu_test_arguments({"app","--cpu-asset-dragdrop-test","/tmp/proof","--workspace"}));
 QMainWindow window;window.setWindowTitle("mPainter — Asset drag/drop regression proof");auto *center=new QWidget;auto *layout=new QHBoxLayout(center);window.setCentralWidget(center);
 auto *view=new QWidget;view->setMinimumSize(540,360);view->setStyleSheet("background:#30343a");layout->addWidget(view);
 auto *layers=new LayerStackList(center);layers->setMinimumWidth(280);layout->addWidget(layers);
 for(const char *name:{"Clear coat","Paint","Base material"}){auto *row=new QListWidgetItem(name,layers);row->setData(Qt::UserRole,QUuid::createUuid().toString());row->setSizeHint({250,52});}
 auto *progress=new AssetApplyProgress(&window);window.statusBar()->addPermanentWidget(progress);window.resize(900,480);window.show();app.processEvents();
 const auto proof=argc>1?QString::fromLocal8Bit(argv[1]):QString{};if(!proof.isEmpty())QDir().mkpath(proof);
 const auto capture=[&](const QString &name){app.processEvents();if(!proof.isEmpty())assert(window.grab().save(proof+"/"+name+".png"));};
 QMimeData mime;mime.setData("application/x-bts-asset","test-material");layers->asset_accepted=[](const QString &id){return id=="test-material";};
 int received=-1;layers->asset_dropped=[&](const QString &id,int gap,bool replace,int,bool){assert(id=="test-material"&&!replace);received=gap;};
 for(int gap=0;gap<=layers->count();++gap){const QPoint point(100,gap==layers->count()?layers->visualItemRect(layers->item(gap-1)).bottom()+20:layers->visualItemRect(layers->item(gap)).top()+2);enter(layers->viewport(),point,mime);assert(layers->insertion_row()==gap);if(gap==1)capture("01-layer-insertion");drop(layers->viewport(),point,mime);assert(received==gap&&layers->insertion_row()==-1);}
 // Asset drops use a gap, never the layer-reordering folder highlight.
 layers->item(1)->setData(Qt::UserRole+3,true);const auto folder=layers->visualItemRect(layers->item(1)).center();enter(layers->viewport(),folder,mime);assert(layers->insertion_row()>=1);QDragLeaveEvent leave;QApplication::sendEvent(layers->viewport(),&leave);assert(layers->insertion_row()==-1);
 auto *effect=new QListWidgetItem("Mask effect");effect->setData(Qt::UserRole+10,QUuid::createUuid().toString());effect->setSizeHint({250,26});layers->insertItem(1,effect);enter(layers->viewport(),layers->visualItemRect(effect).center(),mime);assert(layers->insertion_row()==2);QApplication::sendEvent(layers->viewport(),&leave);delete layers->takeItem(1);
 SceneSnapshot scene;scene.object_names={{11,"Left mesh"},{22,"Right mesh"},{33,"Occluded mesh"}};scene.materials.resize(3);scene.uv_triangles={triangle(-1.3f,0,0,11),triangle(1.3f,0,1,22),triangle(-1.3f,-2,2,33)};
 assert(pick_asset_mesh(scene,{-1.3f,0,5},{0,0,-1})->object==11);assert(pick_asset_mesh(scene,{1.3f,0,5},{0,0,-1})->material==1);assert(!pick_asset_mesh(scene,{0,2,5},{0,0,-1}));assert(!pick_asset_mesh(scene,{0,0,-5},{0,0,-1}));
 auto *receiver=new AssetViewportDrop(view);receiver->accepts=[](const QString &id){return id=="test-material";};
 receiver->pick=[&](QPointF p){return pick_asset_mesh(scene,{float((p.x()-270)/100),float((180-p.y())/100),5},{0,0,-1});};
 receiver->silhouette=[&](const AssetMeshHit &hit){QPainterPath path;for(const auto &t:scene.uv_triangles)if(std::uint32_t(t.triangle_id>>32)==hit.object){QPolygonF polygon;for(const auto &p:t.positions)polygon<<QPointF(270+p[0]*100,180-p[1]*100);path.addPolygon(polygon);path.closeSubpath();}return path;};
 int applications=0;AssetMeshHit applied;receiver->apply=[&](const QString &id,const AssetMeshHit &hit){assert(id=="test-material");++applications;applied=hit;};
 enter(view,{140,180},mime);assert(receiver->hovered()->object==11);capture("02-viewport-mesh-hover");
 QDragMoveEvent move({400,180},Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&move);assert(move.isAccepted()&&receiver->hovered()->material==1);drop(view,{400,180},mime);assert(applications==1&&applied.object==22&&!receiver->hovered());
 enter(view,{140,180},mime);QApplication::sendEvent(view,&leave);assert(!receiver->hovered());
 enter(view,{140,180},mime);drop(view,{400,180},mime);assert(applications==2&&applied.material==1);
 enter(view,{140,180},mime);QDragMoveEvent miss({270,5},Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&miss);assert(!miss.isAccepted()&&!receiver->hovered());QDropEvent missed({270,5},Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&missed);assert(!missed.isAccepted()&&applications==2);
 int progress_signals=0,heartbeats=0,completions=0;bool pending=true;QObject::connect(progress,&QProgressBar::valueChanged,&window,[&](int){++progress_signals;});QTimer heartbeat;heartbeat.setInterval(2);QObject::connect(&heartbeat,&QTimer::timeout,&window,[&]{++heartbeats;});heartbeat.start();progress->pending=[&]{return pending;};progress->completed=[&](const QString &,bool){++completions;};
 assert(progress->begin("Painted steel"));assert(QApplication::overrideCursor()->shape()==Qt::BusyCursor);assert(!progress->begin("Second material"));progress->stage("Load graph");assert(progress->maximum()==0);progress->dispatched();wait(40);assert(progress->active()&&heartbeats>5&&completions==0);
 progress->stage("Decode textures",2,5);assert(progress->maximum()==5&&progress->value()==2);progress->stage("Evaluate channels",1);capture("03-material-progress");progress->stage("Upload",2);progress->stage("Composite",3);pending=false;wait(40);assert(!progress->active()&&!QApplication::overrideCursor()&&completions==1&&progress->value()==4&&progress_signals>=4);capture("04-material-complete");
 assert(progress->begin("Broken material"));progress->fail("Invalid source graph");assert(!progress->active()&&!QApplication::overrideCursor()&&progress->format().contains("Invalid source graph")&&completions==2);capture("05-material-error");
 progress->pending=[&]{progress->fail("Project changed");return true;};assert(progress->begin("Cancelled material"));progress->dispatched();wait(40);assert(!progress->active()&&!QApplication::overrideCursor()&&completions==3&&progress->format().contains("Project changed"));
 {AssetApplyProgress temporary(nullptr);assert(temporary.begin("Closing project"));}assert(!QApplication::overrideCursor());

 std::puts("Asset drag/drop: all stack gaps, effect/folder rows, nearest mesh across sets, miss/leave cleanup, progress signals, heartbeat, errors and cursor lifetime PASS");
}
