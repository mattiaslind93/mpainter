#pragma once
#include "TextureDocument.hh"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <functional>
#include <set>
namespace bts {
class GeometryMaskPanel final:public QWidget {
 public:
  explicit GeometryMaskPanel(QWidget *parent):QWidget(parent){
    setObjectName("geometryMaskProperties");auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    heading_=new QLabel("GEOMETRY MASK",this);layout->addWidget(heading_);
    type_=new QComboBox(this);type_->setObjectName("geometryMaskType");type_->addItem("Mesh names",0);type_->addItem("UV tiles",1);layout->addWidget(type_);
    connect(type_,&QComboBox::currentIndexChanged,this,[this](int index){if(type_changed)type_changed(layer_,type_->itemData(index).toInt());});
    count_=new QLabel(this);count_->setObjectName("geometryMaskCount");layout->addWidget(count_);
    auto *buttons=new QHBoxLayout;for(bool include:{true,false}){auto *button=new QPushButton(include?"Include all":"Exclude all",this);button->setObjectName(include?"geometryIncludeAll":"geometryExcludeAll");buttons->addWidget(button);connect(button,&QPushButton::clicked,this,[this,include]{if(all_changed)all_changed(layer_,include);});}layout->addLayout(buttons);
    meshes_=new QListWidget(this);meshes_->setObjectName("geometryMeshList");meshes_->setMinimumHeight(120);meshes_->setMaximumHeight(260);layout->addWidget(meshes_);
    connect(meshes_,&QListWidget::itemChanged,this,[this](QListWidgetItem *item){if(!item)return;const auto key=item->data(Qt::UserRole);const bool include=item->checkState()==Qt::Checked;if(tile_mode_){if(tile_changed)tile_changed(layer_,key.toInt(),include);}else if(mesh_changed)mesh_changed(layer_,key.toString(),include);});
  }
  std::function<void(QUuid,const QString &,bool)> mesh_changed;
  std::function<void(QUuid,int,bool)> tile_changed;
  std::function<void(QUuid,int)> type_changed;
  std::function<void(QUuid,bool)> all_changed;
  void synchronize(const TextureSet *set,const TextureLayer *layer,bool tiles_available=false){
    setVisible(set&&layer);setEnabled(set&&set->uniform_editable);if(!set||!layer)return;layer_=layer->id;tile_mode_=layer->geometry.type==1;
    {const QSignalBlocker block(type_);type_->setCurrentIndex(layer->geometry.type);type_->setEnabled(tiles_available||tile_mode_);}
    heading_->setText(tile_mode_?"UV TILES MASK":"GEOMETRY MASK");
    std::vector<std::pair<QVariant,bool>> entries;
    if(tile_mode_)for(int tile:set->tiles)entries.push_back({tile,layer->geometry.includes_tile(tile)});
    else{std::set<std::string> names;for(const auto &[uid,name]:set->geometry_names)names.insert(name);for(const auto &name:names)entries.push_back({QString::fromStdString(name),layer->geometry.includes(name)});}
    const QSignalBlocker block(meshes_);bool same=meshes_->count()==int(entries.size());for(int row=0;row<int(entries.size())&&same;++row)same=meshes_->item(row)->data(Qt::UserRole)==entries[row].first;
    if(!same)meshes_->clear();int included=0;
    for(int row=0;row<int(entries.size());++row){const auto &[key,include]=entries[row];auto *item=same?meshes_->item(row):new QListWidgetItem(key.toString(),meshes_);item->setData(Qt::UserRole,key);item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsUserCheckable);item->setCheckState(include?Qt::Checked:Qt::Unchecked);included+=include;}
    count_->setText(QString("%1 / %2 included").arg(included).arg(entries.size()));
  }
 private:
  QUuid layer_;QLabel *count_,*heading_;QComboBox *type_;QListWidget *meshes_;bool tile_mode_=false;
};
}
