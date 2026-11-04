#include "Theme.hh"
#include "BlendMaterialDialog.hh"
#include "FileDialogs.hh"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPromise>
#include <QPushButton>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>
namespace bts {
void choose_blend_material(QWidget *owner,std::function<std::shared_future<PreparedBlendMaterial>(const QString &,const QString &,std::shared_ptr<std::atomic_bool>)> import,std::function<void(PreparedBlendMaterial)> install,bool native){
 auto *chooser=FileDialog::borrow(owner,"Import Blender material",{},"Blender library (*.blend *.BLEND)");chooser->setObjectName("blendMaterialFileDialog");chooser->setAcceptMode(QFileDialog::AcceptOpen);chooser->setFileMode(QFileDialog::ExistingFile);
 if(chooser->exec()!=QDialog::Accepted||chooser->selectedFiles().isEmpty())return;
 const auto path=chooser->selectedFiles().first();FileDialog::remember_folder_async(QFileInfo(path).absolutePath());
 auto *dialog=new QDialog(owner);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName("blendMaterialDialog");dialog->setWindowTitle(native?"Import native Blender shader":"Import Blender material — channel preset");dialog->resize(560,500);
 auto *layout=new QVBoxLayout(dialog);auto *info=new QLabel("Principled Base Color, Metallic and Roughness: constants or direct Image Textures on the active UV map. Scalar images use raw grayscale data. Images load in the background. No live shader graph or other BSDF lobes are transferred; your Asset Library is never modified.",dialog);if(native)info->setText("Import the top shader group and its artist parameters. Images decode in the background; GPU channels prepare before the material appears in Shelf. UV mapping is supported. Unsupported channels are reported explicitly; source libraries are never modified.");info->setWordWrap(true);layout->addWidget(info);
 auto *list=new QListWidget(dialog);list->setObjectName("blendMaterialList");list->setIconSize({64,64});list->setEnabled(false);layout->addWidget(list);
 auto *status=new QLabel("Reading material names and saved previews…",dialog);status->setObjectName("blendMaterialStatus");status->setWordWrap(true);layout->addWidget(status);
 auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,dialog);auto *accept=buttons->button(QDialogButtonBox::Ok);accept->setText("Import to Shelf");accept->setObjectName("importBlendMaterial");accept->setEnabled(false);layout->addWidget(buttons);
 QObject::connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
 QObject::connect(list,&QListWidget::currentItemChanged,dialog,[accept](auto *item,auto *){accept->setEnabled(item!=nullptr);});
 struct ImportState{std::shared_future<PreparedBlendMaterial> future;std::shared_ptr<std::atomic_bool> cancel=std::make_shared<std::atomic_bool>(false);};auto pending=std::make_shared<ImportState>();auto *poll=new QTimer(dialog);poll->setInterval(16);
 QObject::connect(dialog,&QDialog::rejected,dialog,[=]{pending->cancel->store(true);owner->setProperty("blendMaterialImporting",false);});
 QObject::connect(dialog,&QObject::destroyed,[pending]{pending->cancel->store(true);});
 QObject::connect(buttons,&QDialogButtonBox::accepted,dialog,[=]{if(!list->currentItem()||pending->future.valid())return;try{pending->cancel=std::make_shared<std::atomic_bool>(false);pending->future=import(path,list->currentItem()->data(Qt::UserRole).toString(),pending->cancel);list->setEnabled(false);accept->setEnabled(false);status->setStyleSheet({});status->setText("Loading material images… Cancel leaves the document unchanged.");owner->setProperty("blendMaterialImporting",true);poll->start();}catch(const std::exception &error){status->setText(QString::fromUtf8(error.what()));status->setStyleSheet(bts::theme::qss("color: @Error@"));}});
 QObject::connect(poll,&QTimer::timeout,dialog,[=]{if(!pending->future.valid()||pending->future.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;poll->stop();owner->setProperty("blendMaterialImporting",false);try{if(!pending->cancel->load()){install(pending->future.get());dialog->accept();}}catch(const std::exception &error){status->setText(QString::fromUtf8(error.what()));status->setStyleSheet(bts::theme::qss("color: @Error@"));list->setEnabled(true);accept->setEnabled(list->currentItem()!=nullptr);}pending->future={};});
 struct Result{std::vector<BlendMaterialEntry> entries;QString error;};auto promise=std::make_shared<QPromise<Result>>();auto cancel=std::make_shared<std::stop_source>();auto *watcher=new QFutureWatcher<Result>(dialog);
 QObject::connect(dialog,&QObject::destroyed,[cancel]{cancel->request_stop();});
 QObject::connect(watcher,&QFutureWatcher<Result>::finished,dialog,[=]{const auto result=watcher->result();if(!result.error.isEmpty()){status->setText(result.error);return;}for(const auto &entry:result.entries){auto *item=new QListWidgetItem(entry.name+(entry.marked_asset?"  [Asset]":""),list);item->setData(Qt::UserRole,entry.name);if(!entry.preview.isNull())item->setIcon(QPixmap::fromImage(entry.preview));item->setToolTip(entry.marked_asset?"Marked as asset in source Blender file":"Material datablock");}list->setEnabled(true);status->setText(result.entries.empty()?"No materials in this file":"Select a material. Unsupported inputs are reported here.");});
 promise->start();watcher->setFuture(promise->future());QThreadPool::globalInstance()->start([=]{Result result;try{result.entries=inspect_blend_materials(path,cancel->get_token());}catch(const std::exception &error){result.error=QString::fromUtf8(error.what());}promise->addResult(std::move(result));promise->finish();});dialog->open();
}
}
