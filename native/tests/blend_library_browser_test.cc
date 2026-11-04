#include "BlendAssetBrowser.hh"
#include "BlendLibraryPreferences.hh"
#include <QApplication>
#include <QDialog>
#include <QInputDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>
#include <thread>
#include <QElapsedTimer>
#include <cassert>
using namespace bts;
namespace bts {QImage blend_material_preview(const StudioAsset &){return {};}}
int main(int argc,char **argv){QTemporaryDir temp;assert(temp.isValid());qputenv("XDG_CONFIG_HOME",temp.path().toUtf8());qputenv("QT_QPA_PLATFORM","offscreen");QApplication app(argc,argv);save_material_library_paths({temp.path()});QWidget owner;int prepares=0,installs=0,pings=0;double maxgap=0;QElapsedTimer elapsed;elapsed.start();QTimer ping;QObject::connect(&ping,&QTimer::timeout,[&]{++pings;maxgap=std::max(maxgap,elapsed.nsecsElapsed()/1e6);elapsed.restart();});ping.start(10);
 BlendAssetBrowserHooks hooks;hooks.native_shader=true;hooks.scan=[](const QString &path,auto stop,auto){return std::async(std::launch::async,[path,stop]{std::this_thread::sleep_for(std::chrono::milliseconds(80));assert(!stop->stop_requested());BlendAssetLibrary library;library.root=path;library.files=1;for(int i=0;i<110;++i){BlendMaterialEntry e;e.name=QString("Material %1").arg(i);e.marked_asset=true;if(!i){e.preview=QImage(64,64,QImage::Format_RGBA8888);e.preview.fill(Qt::red);}library.materials.push_back({path+"/explicit.blend","explicit.blend",{},std::move(e)});}return library;}).share();};
 hooks.prepare=[&](const QString &,const QString &,auto,bool preview){assert(!preview);++prepares;std::promise<PreparedBlendMaterial> result;PreparedBlendMaterial p;p.material.name="Chosen";result.set_value(std::move(p));return result.get_future().share();};hooks.install=[&](PreparedBlendMaterial p){assert(p.material.name=="Chosen");++installs;};
 QTimer chooser;QObject::connect(&chooser,&QTimer::timeout,[&]{for(auto *w:QApplication::topLevelWidgets())if(auto *d=qobject_cast<QInputDialog *>(w);d&&d->isVisible()){d->accept();chooser.stop();}});chooser.start(5);browse_blend_asset_library(&owner,std::move(hooks));
 QDialog *browser=owner.findChild<QDialog *>("blendAssetLibraryBrowser");assert(browser);QElapsedTimer deadline;deadline.start();while(!browser->property("rowsReady").toBool()){assert(deadline.elapsed()<10000);app.processEvents(QEventLoop::AllEvents,20);QThread::msleep(1);}auto *list=browser->findChild<QListWidget *>("blendLibraryMaterials");assert(list&&list->count()==110);list->setCurrentRow(109);list->scrollToBottom();auto *search=browser->findChild<QLineEdit *>("blendLibrarySearch");search->setText("Material 1");
 QTimer::singleShot(250,&app,[&]{assert(prepares==0&&installs==0&&browser->property("generatedPreviews").toInt()==0);assert(!browser->property("previewPending").toBool());browser->grab().save(qEnvironmentVariable("BTS_TEST_OUTPUT",temp.path())+"/native-library-browser.png");browser->findChild<QPushButton *>("blendLibraryImport")->click();});
 QTimer poll;QObject::connect(&poll,&QTimer::timeout,[&]{if(!installs)return;assert(prepares==1&&installs==1);browser->reject();app.quit();});poll.start(10);QTimer::singleShot(10000,&app,[]{qFatal("Browser test timeout");});app.exec();assert(pings>10&&prepares==1&&installs==1&&maxgap<100);printf("PASS native browse zero preparations; explicit import=1; heartbeat_max_ms=%.3f pings=%d\n",maxgap,pings);
}
