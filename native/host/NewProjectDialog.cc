#include "TextureExtentChoices.hh"
#include "NewProjectDialog.hh"
#include "ProjectTemplate.hh"
#include "AutoUnwrap.hh"
#include "UsdScenePanel.hh"
#include "BlendLibraryPanel.hh"
#include "UnitsPanel.hh"
#include <QScrollArea>

#include <array>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include "FileDialogs.hh"
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QCoreApplication>
#include <QShowEvent>
#include <QVBoxLayout>

namespace bts {
namespace {

constexpr std::array normal_formats = {NormalMapFormat::OpenGL, NormalMapFormat::DirectX};

const char *stylesheet = R"(
  QDialog#newProjectDialog { background: #333333; color: #bcbcbc; }
  QLabel { color: #bcbcbc; background: transparent; }
  QLabel#projectSection { color: #cccccc; font-weight: 600; }
  QLabel#projectValidationError { color: #f09b91; }
  QLabel#uvWorkflowDescription { color: #999999; }
  QLineEdit, QComboBox {
    background: #262626; color: #bcbcbc; border: 1px solid #494949;
    border-radius: 1px; min-height: 24px; padding: 0 6px;
  }
  QLineEdit:focus, QComboBox:focus { border-color: #5580ab; }
  QComboBox QAbstractItemView {
    background: #262626; color: #bcbcbc; selection-background-color: #5580ab;
    selection-color: white; border: 1px solid #494949;
  }
  QCheckBox { color: #bcbcbc; spacing: 7px; }
  QPushButton {
    color: #cccccc; background: #494949; border: 1px solid #494949;
    border-radius: 1px; min-height: 24px; min-width: 76px; padding: 0 10px;
  }
  QPushButton:hover { background: #555555; }
  QPushButton:pressed { background: #262626; }
  QPushButton:focus { border-color: #5580ab; }
  QPushButton:default { border-color: #bcbcbc; }
  QPushButton:disabled { color: #656565; background: #3b3b3b; border-color: #494949; }
  QToolTip { background: #262626; color: #cccccc; border: 1px solid #494949; }
)";

QLabel *section(const QString &text, QWidget *parent)
{
  auto *label = new QLabel(text, parent);
  label->setObjectName("projectSection");
  return label;
}

QString default_shaderball_mesh_path()
{
  const QString app_dir = QCoreApplication::applicationDirPath();
  const QStringList candidates = {
      QDir(app_dir).filePath("assets/meshes/sphere.obj"),
      QDir(app_dir).filePath("../share/mpainter/assets/meshes/sphere.obj"),
      QDir(app_dir).filePath("../../mpainter/assets/meshes/sphere.obj"),
      QDir(app_dir).filePath("../../../mpainter/assets/meshes/sphere.obj"),
      QDir::current().filePath("assets/meshes/sphere.obj"),
  };
  for (const QString &candidate : candidates) {
    const QFileInfo info(candidate);
    if (info.exists() && info.isFile()) {
      return QDir::cleanPath(info.absoluteFilePath());
    }
  }
  return {};
}

}  // namespace

NewProjectDialog::NewProjectDialog(QWidget *parent) : QDialog(parent)
{
  setObjectName("newProjectDialog");
  setWindowTitle(tr("New project"));
  setWindowModality(Qt::WindowModal);
  setMinimumWidth(500);
  resize(545, 700);

  QFont body_font = font();
  body_font.setPixelSize(11);
  setFont(body_font);
  QPalette colors = palette();
  colors.setColor(QPalette::Window, QColor("#333333"));
  colors.setColor(QPalette::WindowText, QColor("#bcbcbc"));
  colors.setColor(QPalette::Base, QColor("#262626"));
  colors.setColor(QPalette::Text, QColor("#bcbcbc"));
  colors.setColor(QPalette::Button, QColor("#494949"));
  colors.setColor(QPalette::ButtonText, QColor("#cccccc"));
  colors.setColor(QPalette::Highlight, QColor("#5580ab"));
  colors.setColor(QPalette::HighlightedText, Qt::white);
  colors.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#656565"));
  setPalette(colors);
  setStyleSheet(QString::fromLatin1(stylesheet));

  auto *outer = new QVBoxLayout(this);auto *scroll=new QScrollArea(this);scroll->setObjectName("newProjectScroll");scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto *body=new QWidget(scroll);body->setObjectName("newProjectBody");scroll->setWidget(body);outer->addWidget(scroll,1);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(14);
  auto *file_row = new QHBoxLayout;
  file_row->setSpacing(8);
  template_=new QComboBox(this);template_->setObjectName("projectTemplate");template_->addItem("Select template…","");template_->addItem("PBR - Metallic Roughness (BTS)",":pbr");template_->addItem("OpenPBR 1.1 — Principled compatibility (BTS)",":openpbr-compatibility");auto *template_row=new QHBoxLayout;template_row->addWidget(new QLabel("Template",this));template_row->addWidget(template_,1);auto *load_template=new QPushButton("Browse…",this);load_template->setObjectName("browseProjectTemplate");template_row->addWidget(load_template);layout->addLayout(template_row);
  for(const auto &name:QDir(project_template_directory()).entryList({"*.btst"},QDir::Files,QDir::Name)){const auto path=QDir(project_template_directory()).absoluteFilePath(name);try{read_project_template(path);template_->addItem(QFileInfo(name).completeBaseName()+" (user)",path);}catch(const std::exception &){}}
  auto *file_label = new QLabel(tr("&File"), this);
  mesh_path_ = new QLineEdit(this);
  mesh_path_->setObjectName("meshPath");
  mesh_path_->setReadOnly(true);
  mesh_path_->setPlaceholderText(tr("Select a mesh scene or Blender library"));
  mesh_path_->setAccessibleName(tr("Mesh file path"));
  file_label->setBuddy(mesh_path_);
  select_mesh_ = new QPushButton(tr("&Select…"), this);
  select_mesh_->setObjectName("selectMesh");
  select_mesh_->setAutoDefault(false);
  auto *shaderball_btn = new QPushButton(tr("Sphere"), this);
  shaderball_btn->setObjectName("useShaderballMesh");
  shaderball_btn->setAutoDefault(false);
  shaderball_btn->setToolTip(tr("Use the generated UV sphere"));
  connect(shaderball_btn, &QPushButton::clicked, this, [this] {
    const QString path = default_shaderball_mesh_path();
    if (!path.isEmpty()) {
      mesh_path_->setText(path);
      mesh_path_->setToolTip(path);
      mesh_path_->setCursorPosition(path.size());
    }
  });
  file_row->addWidget(file_label);
  file_row->addWidget(mesh_path_, 1);
  file_row->addWidget(shaderball_btn);
  file_row->addWidget(select_mesh_);
  layout->addLayout(file_row);
  blend_=new BlendLibraryPanel(mesh_path_,{},this);layout->addWidget(blend_);
  blend_->changed=[this]{if(ok_)validate_options();};

  layout->addWidget(section(tr("Project Settings"), this));
  auto *settings = new QFormLayout;
  settings->setContentsMargins(16, 0, 0, 0);
  settings->setHorizontalSpacing(14);
  settings->setVerticalSpacing(10);
  settings->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
  resolution_ = new TextureExtentComboBox(128,this);
  resolution_->setObjectName("documentResolution");
  resolution_->setAccessibleName(tr("Document resolution"));
  resolution_->setCurrentIndex(resolution_->findData(int(DocumentResolution::Pixels1024)));
  resolution_->setMinimumWidth(92);
  settings->addRow(tr("&Document Resolution"), resolution_);
  normal_format_ = new QComboBox(this);
  normal_format_->setObjectName("normalMapFormat");
  normal_format_->setAccessibleName(tr("Normal map format"));
  normal_format_->addItems({QStringLiteral("OpenGL"), QStringLiteral("DirectX")});
  normal_format_->setMinimumWidth(92);
  settings->addRow(tr("&Normal Map Format"), normal_format_);
  tangent_per_fragment_=new QCheckBox("Compute tangent space per fragment",this);tangent_per_fragment_->setObjectName("newTangentPerFragment");settings->addRow(tangent_per_fragment_);
  layout->addLayout(settings);

  import_cameras_=new QCheckBox("Import Cameras",this);import_cameras_->setObjectName("newImportCameras");import_cameras_->setChecked(true);settings->addRow(import_cameras_);
  auto_unwrap_=new QCheckBox("Auto-unwrap",this);auto_unwrap_->setObjectName("newAutoUnwrap");auto *unwrap_row=new QHBoxLayout;unwrap_row->addWidget(auto_unwrap_);auto *unwrap_options=new QPushButton("Options…",this);unwrap_options->setObjectName("newAutoUnwrapOptions");unwrap_options->setAutoDefault(false);unwrap_row->addWidget(unwrap_options);settings->addRow(unwrap_row);connect(unwrap_options,&QPushButton::clicked,this,[this]{edit_auto_unwrap_options(this,unwrap_options_);});
  usd_=new UsdScenePanel(mesh_path_,{},this);layout->addWidget(usd_);
  units_=new UnitsPanel(mesh_path_,{},{},this);layout->addWidget(units_);
  layout->addWidget(section(tr("UV Tile Settings (UDIMs)"), this));
  auto *uv_settings = new QVBoxLayout;
  uv_settings->setContentsMargins(16, 0, 0, 0);
  uv_settings->setSpacing(8);
  preserve_tiles_ = new QCheckBox(tr("Use UV &Tile workflow"), this);
  preserve_tiles_->setObjectName("preserveUvTiles");
  uv_description_ = new QLabel(this);
  uv_description_->setObjectName("uvWorkflowDescription");
  uv_description_->setWordWrap(true);
  uv_description_->setIndent(22);
  uv_settings->addWidget(preserve_tiles_);
  uv_settings->addWidget(uv_description_);
  layout->addLayout(uv_settings);
  update_uv_description();

  auto *map_header=new QHBoxLayout;auto *map_label=new QLabel(tr("Import mesh normal maps and baked maps"),this);map_label->setWordWrap(true);map_header->addWidget(map_label,1);auto *add_maps=new QPushButton(tr("Add…"),this);add_maps->setObjectName("addUtilityMaps");add_maps->setAutoDefault(false);auto *clear_maps=new QPushButton(tr("Clear"),this);clear_maps->setObjectName("clearUtilityMaps");clear_maps->setAutoDefault(false);map_header->addWidget(add_maps);map_header->addWidget(clear_maps);layout->addLayout(map_header);
  utility_maps_=new QListWidget(this);utility_maps_->setObjectName("projectUtilityMaps");utility_maps_->setMinimumHeight(100);utility_maps_->setTextElideMode(Qt::ElideNone);utility_maps_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);layout->addWidget(utility_maps_);
  auto *naming=new QLabel(tr("Naming: TextureSet_normal_base, TextureSet_ambient_occlusion"),this);naming->setWordWrap(true);naming->setToolTip(tr("Mesh-map suffixes: normal_base, world_space_normals, id, ambient_occlusion, curvature, position, thickness. Optional .1001 or _1001 tile number."));layout->addWidget(naming);
  connect(template_,&QComboBox::currentIndexChanged,this,[this]{try{const auto value=template_->currentData().toString();if(value.isEmpty())return;if(builtin_project_template(value.toStdString())){resolution_->setCurrentIndex(resolution_->findData(1024));normal_format_->setCurrentIndex(1);preserve_tiles_->setChecked(false);tangent_per_fragment_->setChecked(false);import_cameras_->setChecked(true);unwrap_options_={};auto_unwrap_->setChecked(false);units_->set_options({});}else{const auto options=project_template_options(read_project_template(value));resolution_->setCurrentIndex(resolution_->findData(int(options.resolution)));normal_format_->setCurrentIndex(options.normal_map_format==NormalMapFormat::DirectX?1:0);preserve_tiles_->setChecked(options.uv_workflow==UvWorkflow::PreserveTiles);tangent_per_fragment_->setChecked(options.tangent_per_fragment);import_cameras_->setChecked(options.import_cameras);unwrap_options_=options.auto_unwrap;auto_unwrap_->setChecked(unwrap_options_.enabled);units_->set_options(options.units);}validate_options();}catch(const std::exception &error){validation_error_->setText(QString::fromUtf8(error.what()));validation_error_->show();ok_->setEnabled(false);}});
  connect(load_template,&QPushButton::clicked,this,[this]{auto *picker=new bts::FileDialog(this,"Select project template",project_template_directory(),"BTS project templates (*.btst)");picker->setObjectName("projectTemplatePicker");picker->setOption(QFileDialog::DontUseNativeDialog);picker->setFileMode(QFileDialog::ExistingFile);picker->setAttribute(Qt::WA_DeleteOnClose);connect(picker,&QFileDialog::fileSelected,this,[this](const QString &path){try{read_project_template(path);const auto absolute=QFileInfo(path).absoluteFilePath();int index=template_->findData(absolute);if(index<0){template_->addItem(QFileInfo(path).completeBaseName()+" (user)",absolute);index=template_->count()-1;}template_->setCurrentIndex(index);}catch(const std::exception &error){validation_error_->setText(QString::fromUtf8(error.what()));validation_error_->show();}});picker->open();});
  connect(add_maps,&QPushButton::clicked,this,[this]{auto *picker=new bts::FileDialog(this,tr("Import images"),QDir::homePath(),tr("Images (*.png *.exr *.tif *.tiff *.tga *.bmp)"));picker->setObjectName("utilityMapPicker");picker->setFileMode(QFileDialog::ExistingFiles);picker->setOption(QFileDialog::DontUseNativeDialog);picker->setAttribute(Qt::WA_DeleteOnClose);connect(picker,&QFileDialog::filesSelected,this,[this](const QStringList &files){for(const auto &path:files){const auto absolute=QDir::cleanPath(QFileInfo(path).absoluteFilePath());if(utility_maps_->findItems(absolute,Qt::MatchExactly).isEmpty()){auto *item=new QListWidgetItem(absolute,utility_maps_);item->setToolTip(absolute);}}validate_options();});picker->open();});
  connect(clear_maps,&QPushButton::clicked,this,[this]{utility_maps_->clear();validate_options();});

  validation_error_ = new QLabel(this);
  validation_error_->setObjectName("projectValidationError");
  validation_error_->setWordWrap(true);
  validation_error_->setTextFormat(Qt::PlainText);
  validation_error_->setVisible(false);
  layout->addWidget(validation_error_);
  layout->addStretch();
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->setObjectName("projectButtons");
  ok_ = buttons->button(QDialogButtonBox::Ok);
  ok_->setObjectName("createProject");
  ok_->setIcon(QIcon());
  ok_->setDefault(true);
  ok_->setEnabled(false);
  buttons->button(QDialogButtonBox::Cancel)->setObjectName("cancelProject");
  buttons->button(QDialogButtonBox::Cancel)->setIcon(QIcon());
  outer->addWidget(buttons);

  connect(select_mesh_, &QPushButton::clicked, this, &NewProjectDialog::choose_mesh);
  connect(mesh_path_, &QLineEdit::textChanged, this, [this] { validate_options(); });
  connect(resolution_, &QComboBox::currentIndexChanged, this, [this] {
    if (!mesh_path_->text().isEmpty()) {
      validate_options();
    }
  });
  connect(normal_format_, &QComboBox::currentIndexChanged, this, [this] {
    if (!mesh_path_->text().isEmpty()) {
      validate_options();
    }
  });
  connect(preserve_tiles_, &QCheckBox::toggled, this, &NewProjectDialog::update_uv_description);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  select_mesh_->setFocus();
}

void NewProjectDialog::choose_mesh()
{
  const QString directory = mesh_path_->text().isEmpty() ?
                                QDir::homePath() : QFileInfo(mesh_path_->text()).absolutePath();
  auto *picker = new bts::FileDialog(this,
                                tr("Select mesh"),
                                directory,
                                tr("Mesh scenes (*.obj *.OBJ *.blend *.BLEND *.abc *.ABC *.dae *.DAE *.fbx *.FBX *.gltf *.GLTF *.glb *.GLB *.usd *.usda *.usdc *.usdz)"));
  picker->setObjectName("meshFilePicker");
  picker->setFileMode(QFileDialog::ExistingFile);
  picker->setAcceptMode(QFileDialog::AcceptOpen);
  picker->setOption(QFileDialog::ReadOnly);
  picker->setAttribute(Qt::WA_DeleteOnClose);
  select_mesh_->setEnabled(false);
  connect(picker, &QFileDialog::fileSelected, this, [this](const QString &path) {
    const QString absolute_path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    mesh_path_->setText(absolute_path);
    mesh_path_->setToolTip(absolute_path);
    mesh_path_->setCursorPosition(absolute_path.size());
  });
  connect(picker, &QDialog::finished, this, [this] { select_mesh_->setEnabled(true); });
  picker->open();
}

std::optional<NewProjectOptions> NewProjectDialog::validate_options()
{
  const QString path = mesh_path_->text();
  const QFileInfo file_info(path);
  QString error;
  if (path.isEmpty()) {
    error = tr("Select an OBJ, Blender, Alembic, FBX, glTF, Collada or USD mesh file.");
  }
  else if (!file_info.isAbsolute() || path.contains(QChar::Null)) {
    error = tr("Select a mesh file with a valid absolute path.");
  }
  else if (!QStringList{"obj","fbx","gltf","glb"}.contains(file_info.suffix(),Qt::CaseInsensitive) && file_info.suffix().compare(QStringLiteral("dae"),Qt::CaseInsensitive)!=0 && file_info.suffix().compare(QStringLiteral("abc"),Qt::CaseInsensitive)!=0 && !is_usd_scene(path.toStdString()) && !is_blend_library(path.toStdString())) {
    error = tr("This project importer supports OBJ, Blender libraries, Alembic, FBX, glTF, Collada and USD scenes.");
  }
  else if (!file_info.exists()) {
    error = tr("The selected mesh file no longer exists.");
  }
  else if (!file_info.isFile()) {
    error = tr("The selected path is not a regular file.");
  }
  else if (!file_info.isReadable()) {
    error = tr("The selected mesh file cannot be read.");
  }
  else {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      error = tr("Cannot read the selected mesh: %1").arg(file.errorString());
    }
  }
  const int resolution_value = resolution_->currentData().toInt();
  const int normal_index = normal_format_->currentIndex();
  if (error.isEmpty() && !valid_sparse_texture_extent(resolution_value,128,true)) {
    error = tr("Select a supported document resolution.");
  }
  if (error.isEmpty() && (normal_index < 0 || normal_index >= int(normal_formats.size()))) {
    error = tr("Select a normal map format.");
  }
  for(int i=0;i<utility_maps_->count()&&error.isEmpty();++i){const auto path=utility_maps_->item(i)->text();const QFileInfo map(path);if(!map.isAbsolute()||path.contains(QChar::Null)||!map.isFile()||!map.isReadable())error=tr("A selected utility map is unavailable. Clear the list or select it again.");}
  validation_error_->setText(error);
  validation_error_->setVisible(!error.isEmpty());
  ok_->setEnabled(error.isEmpty());
  if (!error.isEmpty()) {
    return std::nullopt;
  }
  NewProjectOptions result{QDir::cleanPath(file_info.absoluteFilePath()).toUtf8().toStdString(),
                           static_cast<DocumentResolution>(resolution_value),
                           normal_formats[normal_index],
                           preserve_tiles_->isChecked() ? UvWorkflow::PreserveTiles :
                                                          UvWorkflow::SingleTile};
  for(int i=0;i<utility_maps_->count();++i)result.utility_maps.push_back(utility_maps_->item(i)->text().toUtf8().toStdString());
  try{result.blend=blend_->options();}catch(const std::exception &failure){validation_error_->setText(QString::fromUtf8(failure.what()));validation_error_->show();ok_->setEnabled(false);return {};}
  result.usd=usd_->options();result.units=units_->options();
  result.auto_unwrap=unwrap_options_;result.auto_unwrap.enabled=auto_unwrap_->isChecked();
  result.import_cameras=import_cameras_->isChecked();
  result.tangent_per_fragment=tangent_per_fragment_->isChecked();
  result.template_path=template_->currentData().toString().toStdString();
  if(!result.template_path.empty()&&!builtin_project_template(result.template_path)){try{read_project_template(QString::fromStdString(result.template_path));}catch(const std::exception &failure){validation_error_->setText(QString::fromUtf8(failure.what()));validation_error_->show();ok_->setEnabled(false);return {};}}
  return result;
}

void NewProjectDialog::update_uv_description()
{
  uv_description_->setText(preserve_tiles_->isChecked() ?
                               tr("Preserve the original UV tile layout per material.") :
                               tr("Single tile project (1001)."));
}

std::optional<NewProjectOptions> NewProjectDialog::options() const
{
  return result() == QDialog::Accepted ? accepted_options_ : std::nullopt;
}

void NewProjectDialog::done(const int result)
{
  accepted_options_.reset();
  if (result == QDialog::Accepted) {
    accepted_options_ = validate_options();
    if (!accepted_options_) {
      select_mesh_->setFocus();
      return;
    }
  }
  QDialog::done(result);
}

void NewProjectDialog::showEvent(QShowEvent *event)
{
  accepted_options_.reset();
  setResult(QDialog::Rejected);
  if (!mesh_path_->text().isEmpty()) {
    validate_options();
  }
  QDialog::showEvent(event);
}

}  // namespace bts
