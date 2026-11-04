#include "NumericLocale.hh"
#include "Preferences.hh"
#include "BlendLibraryPreferences.hh"
#include <QListWidget>
#include <QHBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QLineEdit>
#include <atomic>
#include <algorithm>
#include <limits>
namespace bts {
namespace {
// Two positive Qt int budgets need 31 bits each; bit 63 records automatic mode.
constexpr std::uint64_t budget_mask=0x7fffffffULL,automatic_mask=1ULL<<63;
std::atomic_uint64_t &memory_preferences(){
 static std::atomic_uint64_t value{[]{QSettings settings("mPainter","Preferences");const auto mb=std::uint32_t(std::max(1,settings.value("Viewport/textureBudgetMB",512).toInt()));return std::uint64_t(mb)|(std::uint64_t(std::max(0,settings.value("TextureImport/decodedBudgetMB",0).toInt()))<<31)|(settings.value("Viewport/textureBudgetAuto",true).toBool()?automatic_mask:0);}()};
 return value;
}
void update_memory_preferences(std::uint64_t mask,std::uint64_t replacement){
 auto &preferences=memory_preferences();auto old=preferences.load(std::memory_order_relaxed);
 while(!preferences.compare_exchange_weak(old,(old&~mask)|replacement,std::memory_order_relaxed)){}
}
}
ViewportMemoryPreferences viewport_memory_preferences(){const auto value=memory_preferences().load(std::memory_order_relaxed);return {bool(value&automatic_mask),int(value&budget_mask),int((value>>31)&budget_mask)};}
void preview_viewport_memory_preferences(bool automatic,int megabytes){update_memory_preferences(budget_mask|automatic_mask,std::uint64_t(std::max(1,megabytes))|(automatic?automatic_mask:0));}
void preview_texture_loading_budget(int megabytes){update_memory_preferences(budget_mask<<31,std::uint64_t(std::max(0,megabytes))<<31);}
bool load_trackball_navigation(){QSettings settings("mPainter","Preferences");return settings.value("Viewport/trackball",false).toBool();}
void save_trackball_navigation(bool enabled){QSettings settings("mPainter","Preferences");settings.setValue("Viewport/trackball",enabled);settings.sync();}
ReferenceImagePreferences load_reference_image_preferences(){
  QSettings settings("mPainter","Preferences");
  ReferenceImagePreferences result;
  result.enabled=settings.value("Viewport/referenceImageEnabled",false).toBool();
  result.path=settings.value("Viewport/referenceImagePath").toString();
  result.center_x=settings.value("Viewport/referenceImageCenterX",0.5).toDouble();
  result.center_y=settings.value("Viewport/referenceImageCenterY",0.5).toDouble();
  result.vertical_extent=std::clamp(settings.value("Viewport/referenceImageVerticalExtent",1.15).toDouble(),0.01,10000.0);
  return result;
}
void save_reference_image_preferences(const ReferenceImagePreferences &preferences){
  QSettings settings("mPainter","Preferences");
  settings.setValue("Viewport/referenceImageEnabled",preferences.enabled);
  settings.setValue("Viewport/referenceImagePath",preferences.path);
  settings.setValue("Viewport/referenceImageCenterX",preferences.center_x);
  settings.setValue("Viewport/referenceImageCenterY",preferences.center_y);
  settings.setValue("Viewport/referenceImageVerticalExtent",preferences.vertical_extent);
  settings.sync();
}
ViewportClipping load_viewport_clipping(){QSettings settings("mPainter","Preferences");ViewportClipping value{settings.value("Viewport/clipStart",.01).toDouble(),settings.value("Viewport/clipEnd",1000.).toDouble()};return value.valid()?value:ViewportClipping{};}
void show_preferences(QWidget *parent,const QString &shelf,bool project_open,ViewportClipping current,std::function<void(ViewportClipping)> preview){
  QDialog dialog(parent);dialog.setObjectName("shelfPreferencesDialog");dialog.setWindowTitle("Preferences");dialog.resize(600,340);
  auto *layout=new QVBoxLayout(&dialog);auto *tabs=new QTabWidget(&dialog);tabs->setObjectName("preferencesSections");layout->addWidget(tabs);
  auto *shelf_page=new QWidget(tabs);auto *shelf_layout=new QFormLayout(shelf_page);tabs->addTab(shelf_page,"Shelf");
  shelf_layout->addRow(new QLabel("Shelf Locations",shelf_page));if(project_open)shelf_layout->addRow(new QLabel("Shelf locations cannot be changed while a project is open.",shelf_page));
  for(const auto &location:{std::pair{QString("User shelf"),shelf},std::pair{QString("Built-in"),QString("Application generators and material presets")}}){auto *path=new QLineEdit(location.second,shelf_page);path->setReadOnly(true);shelf_layout->addRow(location.first,path);}
  auto *library_page=new QWidget(tabs);auto *library_layout=new QVBoxLayout(library_page);tabs->addTab(library_page,"Material Libraries");
  auto *help=new QLabel("Only paths you add here are indexed. Your Blender default Asset Library is never changed. Open them with File → Browse Blender Asset Library…",library_page);help->setWordWrap(true);library_layout->addWidget(help);
  auto *libraries=new QListWidget(library_page);libraries->setObjectName("materialLibraryPaths");libraries->addItems(material_library_paths());library_layout->addWidget(libraries);
  for(int row=0;row<libraries->count();++row){auto *item=libraries->item(row);item->setToolTip(item->text());if(!QFileInfo(item->text()).exists()){item->setForeground(QColor("#999999"));item->setToolTip("Library is offline or missing: "+item->text());}}
  auto *library_row=new QHBoxLayout;auto *library_path=new QLineEdit(library_page);library_path->setObjectName("materialLibraryPath");library_path->setPlaceholderText("Absolute folder or .blend path");auto *library_add=new QPushButton("Add",library_page);library_add->setObjectName("addMaterialLibrary");auto *library_remove=new QPushButton("Remove",library_page);library_remove->setObjectName("removeMaterialLibrary");library_row->addWidget(library_path,1);library_row->addWidget(library_add);library_row->addWidget(library_remove);library_layout->addLayout(library_row);
  auto *library_error=new QLabel(library_page);library_error->setObjectName("materialLibraryError");library_error->setStyleSheet("color: #ee8888");library_error->setWordWrap(true);library_layout->addWidget(library_error);
  QObject::connect(library_add,&QPushButton::clicked,&dialog,[&]{try{const auto path=validated_material_library_path(library_path->text());if(libraries->findItems(path,Qt::MatchExactly).isEmpty()){require_texture_ram(resource_sum(resource_product(libraries->count()+1,sizeof(QListWidgetItem)+sizeof(QString)),resource_product(path.size(),sizeof(QChar))),"Material library Preferences rows");libraries->addItem(path);}library_path->clear();library_error->clear();}catch(const std::exception &error){library_error->setText(QString::fromUtf8(error.what()));}});
  QObject::connect(library_remove,&QPushButton::clicked,&dialog,[&]{delete libraries->takeItem(libraries->currentRow());});
  auto *viewport_page=new QWidget(tabs);auto *form=new QFormLayout(viewport_page);tabs->addTab(viewport_page,"Viewport");
  const auto number=[&](const char *name,double value){auto *spin=new bts::NumericSpinBox(viewport_page);spin->setObjectName(name);spin->setLocale(QLocale::c());spin->setDecimals(6);spin->setRange(ViewportClipping::minimum_near,ViewportClipping::maximum_plane);spin->setValue(value);spin->setKeyboardTracking(false);return spin;};
  auto *near=number("viewportClipStart",current.near_plane),*far=number("viewportClipEnd",current.far_plane);near->setSingleStep(.01);far->setSingleStep(100);form->addRow("Clip Start",near);form->addRow("Clip End",far);
  QSettings preferences("mPainter","Preferences");const bool old_auto=preferences.value("Viewport/textureBudgetAuto",true).toBool();const int old_resolution=preferences.value("Viewport/maxTextureResolution",0).toInt();const int old_budget=std::max(1,preferences.value("Viewport/textureBudgetMB",512).toInt());auto *budget=new QSpinBox(viewport_page);budget->setObjectName("viewportTextureBudgetMB");budget->setRange(1,std::numeric_limits<int>::max());budget->setSuffix(" MiB");budget->setValue(old_budget);budget->setToolTip("Warning threshold for visible full-resolution textures. Memory pressure is reported without lowering texture resolution. Source/stack caches and renderer working memory are separate.");form->addRow("Texture memory warning budget",budget);
  auto *automatic=new QCheckBox("Automatic (resident images + 50% free VRAM)",viewport_page);automatic->setObjectName("viewportTextureBudgetAuto");automatic->setChecked(old_auto);budget->setEnabled(!old_auto);form->insertRow(3,automatic);
  auto *maximum=new QComboBox(viewport_page);maximum->setObjectName("viewportMaxTextureResolution");maximum->addItem("Texture set (always full resolution)",-1);maximum->setEnabled(false);maximum->setToolTip("Painting and layer edits retain the texture set resolution at every frame.");form->addRow("Viewport resolution",maximum);
  const auto texture_preview=[&]{preview_viewport_memory_preferences(automatic->isChecked(),budget->value());QSettings settings("mPainter","Preferences");settings.setValue("Viewport/textureBudgetMB",budget->value());settings.setValue("Viewport/textureBudgetAuto",automatic->isChecked());settings.setValue("Viewport/maxTextureResolution",maximum->currentData());settings.sync();budget->setEnabled(!automatic->isChecked());preview({near->value(),far->value()});};
  QObject::connect(budget,&QSpinBox::valueChanged,&dialog,texture_preview);QObject::connect(automatic,&QCheckBox::toggled,&dialog,texture_preview);QObject::connect(maximum,&QComboBox::currentIndexChanged,&dialog,texture_preview);

  const int old_loading_budget=preferences.value("TextureImport/decodedBudgetMB",0).toInt();auto *loading_budget=new QSpinBox(viewport_page);loading_budget->setObjectName("textureLoadingBudgetMB");loading_budget->setRange(0,std::numeric_limits<int>::max());loading_budget->setSingleStep(512);loading_budget->setSpecialValueText("Auto (RAM budget)");loading_budget->setSuffix(" MiB");loading_budget->setValue(old_loading_budget);loading_budget->setToolTip("Decoded pixels plus parallel tile-worker buffers. Auto reserves 50% of currently available RAM (including the container limit); a positive value is an additional MiB cap. Individual tiles are also checked against available VRAM and the GPU's texture dimensions.");form->addRow("Texture loading memory",loading_budget);QObject::connect(loading_budget,&QSpinBox::valueChanged,&dialog,[loading_budget](int value){preview_texture_loading_budget(value);QSettings settings("mPainter","Preferences");settings.setValue("TextureImport/decodedBudgetMB",value);settings.sync();});
  auto *hint=new QLabel("Free orbit view only. Imported cameras retain their own clipping until you navigate.\nOrthographic depth spans ±Clip End around the view.",viewport_page);hint->setWordWrap(true);form->addRow(hint);
  auto *error=new QLabel(viewport_page);error->setObjectName("viewportClipError");error->setStyleSheet("color:#ee9a83");error->setWordWrap(true);form->addRow(error);
  auto *restore=new QPushButton("Restore viewport defaults",viewport_page);restore->setObjectName("restoreViewportClipping");form->addRow(restore);
  auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);
  const auto update=[&]{ViewportClipping value{near->value(),far->value()};const bool valid=value.valid();buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);error->setText(valid?QString{}:"Clip Start must be positive and smaller than Clip End.");if(valid)preview(value);};
  QObject::connect(near,&QDoubleSpinBox::valueChanged,&dialog,update);QObject::connect(far,&QDoubleSpinBox::valueChanged,&dialog,update);
  QObject::connect(restore,&QPushButton::clicked,&dialog,[&]{far->setValue(1000);near->setValue(.01);automatic->setChecked(true);maximum->setCurrentIndex(maximum->findData(-1));});
  QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  if(dialog.exec()==QDialog::Accepted){QStringList paths;for(int row=0;row<libraries->count();++row)paths.append(libraries->item(row)->text());save_material_library_paths(paths);QSettings settings("mPainter","Preferences");settings.setValue("Viewport/clipStart",near->value());settings.setValue("Viewport/clipEnd",far->value());settings.sync();}else {preview_viewport_memory_preferences(old_auto,old_budget);preview_texture_loading_budget(old_loading_budget);QSettings settings("mPainter","Preferences");settings.setValue("TextureImport/decodedBudgetMB",old_loading_budget);settings.setValue("Viewport/textureBudgetMB",old_budget);settings.setValue("Viewport/textureBudgetAuto",old_auto);settings.setValue("Viewport/maxTextureResolution",old_resolution);settings.sync();preview(current);}
}
}
