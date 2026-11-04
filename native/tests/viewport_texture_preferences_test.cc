#include "BlendLibraryPreferences.hh"
#include <QListWidget>
#include <QLineEdit>
#include <QFile>
#include <QTabWidget>
#include "Preferences.hh"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <cassert>
#include <QLockFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonObject>
#include <QJsonDocument>
#include <future>
#include <chrono>
#include <cstdio>
#include <thread>
#include <limits>
using namespace bts;
QJsonObject verify_runtime_preferences_snapshot(){
 const auto previous=viewport_memory_preferences();preview_viewport_memory_preferences(false,4096);
 QString path;{QSettings settings("mPainter","Preferences");path=settings.fileName();}
 QDir().mkpath(QFileInfo(path).absolutePath());QLockFile blocked(path+".lock");blocked.setStaleLockTime(0);if(!blocked.tryLock(0))throw std::runtime_error("Cannot isolate settings-lock regression");
 std::promise<void> started;auto ready=started.get_future();auto writer=std::async(std::launch::async,[&]{QSettings settings("mPainter","Preferences");settings.setValue("AE/settingsLockRegression",true);started.set_value();settings.sync();});ready.wait();std::this_thread::sleep_for(std::chrono::milliseconds(20));assert(writer.wait_for(std::chrono::milliseconds(0))==std::future_status::timeout);
 const auto begin=std::chrono::steady_clock::now();bool valid=true;for(int i=0;i<10000;++i){const auto value=viewport_memory_preferences();valid&=!value.automatic&&value.megabytes==4096;}
 const double milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();blocked.unlock();writer.get();preview_viewport_memory_preferences(previous.automatic,previous.megabytes);
 if(!valid||milliseconds>=50)throw std::runtime_error("Viewport preference reads waited on a blocked settings writer");
 return {{"reads",10000},{"settings_file_locked",true},{"runtime_change_visible",valid},{"cached_read_ms",milliseconds},{"render_thread_settings_io",false}};
}

int main(int argc,char **argv){
 QTemporaryDir directory("/tmp/mpainter-tests/viewport-preferences-XXXXXX");assert(directory.isValid());qputenv("XDG_CONFIG_HOME",directory.path().toUtf8());qputenv("QT_QPA_PLATFORM","offscreen");QApplication app(argc,argv);int previews=0;
 // Absent global navigation preference preserves legacy turntable; both toggle
 // values survive an independent settings instance without document state.
 {QSettings initial("mPainter","Preferences");initial.remove("Viewport/trackball");initial.sync();}
 assert(!load_trackball_navigation());save_trackball_navigation(true);assert(load_trackball_navigation());
 {QSettings saved("mPainter","Preferences");assert(saved.value("Viewport/trackball").toBool());}
 save_trackball_navigation(false);assert(!load_trackball_navigation());
 // Loading persisted values must retain all 31 positive Qt-int bits.
 {QSettings initial("mPainter","Preferences");initial.setValue("Viewport/textureBudgetMB",131072);initial.setValue("TextureImport/decodedBudgetMB",262144);initial.setValue("Viewport/textureBudgetAuto",false);initial.sync();}
 assert(viewport_memory_preferences().megabytes==131072&&viewport_memory_preferences().decode_megabytes==262144&&!viewport_memory_preferences().automatic);
 {QSettings initial("mPainter","Preferences");initial.clear();}preview_viewport_memory_preferences(true,512);preview_texture_loading_budget(0);
 assert(material_library_paths().isEmpty());const QString folder=directory.path()+"/Library";assert(QDir().mkpath(folder));const QString file=directory.path()+"/Material.blend";{QFile source(file);assert(source.open(QIODevice::WriteOnly)&&source.write("synthetic path fixture")==22);}
 bool invalid=false;try{validated_material_library_path("relative.blend");}catch(const std::invalid_argument &){invalid=true;}assert(invalid);
 QTimer::singleShot(0,&app,[&]{auto *d=qobject_cast<QDialog *>(QApplication::activeModalWidget());assert(d);auto *list=d->findChild<QListWidget *>("materialLibraryPaths");auto *path=d->findChild<QLineEdit *>("materialLibraryPath");auto *add=d->findChild<QPushButton *>("addMaterialLibrary");assert(list&&path&&add);path->setText(folder);add->click();path->setText(folder+"/.");add->click();assert(list->count()==1);path->setText(file);add->click();assert(list->count()==2);auto *tabs=d->findChild<QTabWidget *>("preferencesSections");tabs->setCurrentIndex(1);const auto proof=qEnvironmentVariable("BTS_TEST_OUTPUT");if(!proof.isEmpty())d->grab().save(proof+"/material-library-preferences.png");d->accept();});
 show_preferences(nullptr,"",false,{},[](auto){});assert(material_library_paths()==QStringList({QFileInfo(folder).canonicalFilePath(),QFileInfo(file).canonicalFilePath()}));
 QTimer::singleShot(0,&app,[&]{auto *d=qobject_cast<QDialog *>(QApplication::activeModalWidget());auto *list=d->findChild<QListWidget *>("materialLibraryPaths");assert(list->count()==2);list->setCurrentRow(0);d->findChild<QPushButton *>("removeMaterialLibrary")->click();assert(list->count()==1);d->reject();});show_preferences(nullptr,"",false,{},[](auto){});assert(material_library_paths().size()==2);QFile::remove(file);assert(material_library_paths().size()==2);

 QTimer::singleShot(0,&app,[&]{auto *d=qobject_cast<QDialog *>(QApplication::activeModalWidget());assert(d);auto *automatic=d->findChild<QCheckBox *>("viewportTextureBudgetAuto");auto *budget=d->findChild<QSpinBox *>("viewportTextureBudgetMB");auto *maximum=d->findChild<QComboBox *>("viewportMaxTextureResolution");assert(automatic->isChecked()&&!budget->isEnabled()&&maximum->currentData().toInt()==-1&&!maximum->isEnabled());automatic->setChecked(false);assert(budget->isEnabled());budget->setValue(256);assert(!viewport_memory_preferences().automatic&&viewport_memory_preferences().megabytes==256);assert(maximum->findData(4096)==-1);QSettings s("mPainter","Preferences");assert(!s.value("Viewport/textureBudgetAuto").toBool()&&s.value("Viewport/maxTextureResolution").toInt()==-1);d->accept();});
 bts::show_preferences(nullptr,"",false,{},[&](auto clipping){assert(clipping.valid());++previews;});assert(previews>=2);
 QTimer::singleShot(0,&app,[&]{auto *d=qobject_cast<QDialog *>(QApplication::activeModalWidget());auto *maximum=d->findChild<QComboBox *>("viewportMaxTextureResolution");assert(maximum->currentData().toInt()==-1&&!maximum->isEnabled());d->findChild<QCheckBox *>("viewportTextureBudgetAuto")->setChecked(true);assert(maximum->findData(2048)==-1);d->findChild<QSpinBox *>("textureLoadingBudgetMB")->setValue(1024);assert(viewport_memory_preferences().decode_megabytes==1024);d->reject();});
 bts::show_preferences(nullptr,"",false,{},[](auto){});QSettings saved("mPainter","Preferences");assert(viewport_memory_preferences().decode_megabytes==0&&!viewport_memory_preferences().automatic&&viewport_memory_preferences().megabytes==256);assert(!saved.value("Viewport/textureBudgetAuto").toBool()&&saved.value("Viewport/maxTextureResolution").toInt()==-1&&saved.value("Viewport/textureBudgetMB").toInt()==256);
 // Stored and visible library collections cross the former 64-entry ceiling.
 QStringList many;for(int i=0;i<64;++i)many.append(directory.path()+"/offline-"+QString::number(i));save_material_library_paths(many);assert(material_library_paths()==many);
 QTimer::singleShot(0,&app,[&]{auto *d=qobject_cast<QDialog *>(QApplication::activeModalWidget());auto *list=d->findChild<QListWidget *>("materialLibraryPaths");assert(list->count()==64);d->findChild<QLineEdit *>("materialLibraryPath")->setText(folder);d->findChild<QPushButton *>("addMaterialLibrary")->click();assert(list->count()==65);auto *budget=d->findChild<QSpinBox *>("viewportTextureBudgetMB"),*loading=d->findChild<QSpinBox *>("textureLoadingBudgetMB");assert(budget->maximum()==std::numeric_limits<int>::max()&&loading->maximum()==std::numeric_limits<int>::max());budget->setValue(131072);loading->setValue(262144);assert(viewport_memory_preferences().megabytes==131072&&viewport_memory_preferences().decode_megabytes==262144);budget->setValue(std::numeric_limits<int>::max());loading->setValue(std::numeric_limits<int>::max());assert(viewport_memory_preferences().megabytes==std::numeric_limits<int>::max()&&viewport_memory_preferences().decode_megabytes==std::numeric_limits<int>::max());budget->setValue(1);loading->setValue(1);assert(viewport_memory_preferences().megabytes==1&&viewport_memory_preferences().decode_megabytes==1);d->accept();});show_preferences(nullptr,"",false,{},[](auto){});assert(material_library_paths().size()==65);
 {TextureRamQueryScope low_memory(+[]()->std::uint64_t{return 0;});bool denied=false;try{material_library_paths();}catch(const std::length_error &){denied=true;}assert(denied);}
 QTimer::singleShot(0,&app,[&]{auto *d=qobject_cast<QDialog *>(QApplication::activeModalWidget());d->findChild<QPushButton *>("restoreViewportClipping")->click();assert(d->findChild<QCheckBox *>("viewportTextureBudgetAuto")->isChecked());assert(d->findChild<QComboBox *>("viewportMaxTextureResolution")->currentData().toInt()==-1);d->accept();});bts::show_preferences(nullptr,"",false,{},[](auto){});std::puts(QJsonDocument(verify_runtime_preferences_snapshot()).toJson(QJsonDocument::Compact).constData());return 0;
}
