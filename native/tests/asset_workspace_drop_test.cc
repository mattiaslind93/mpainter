// SPDX-License-Identifier: GPL-3.0-or-later
#include "WorkspaceShell.hh"
#include "BuiltinPolyHaven.hh"
#include "LayerStackList.hh"
#include "AssetApplyProgress.hh"
#include "AssetThumbnail.hh"
#include "AssetApplicationTarget.hh"
#include "SmartMaterialPreset.hh"
#include <QApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QStatusBar>
#include <QLabel>
#include <cassert>
#include <cstdio>

namespace bts {int run_asset_workspace_drop_test(const QString &directory);}
int bts::run_asset_workspace_drop_test(const QString &directory){
 QTemporaryDir library;AssetCatalog catalog(library.path());StudioAsset material;material.name="Drop test · Painted steel";material.filename="drop-material.builtin";material.usages={"material"};material.material=MaterialValues{};material.material->base_color={.08f,.3f,.7f,1};catalog.add(material);
 StudioAsset smart;smart.name="Drop test · Smart finish";smart.filename="drop-smart.builtin";smart.usages={"smartmaterial"};smart.smart_material=true;auto preset=std::make_shared<SmartMaterialPreset>();TextureLayer group,child;group.kind=LayerKind::Folder;group.name="Smart finish";child.kind=LayerKind::Fill;child.name="Finish";child.parent=group.id;child.depth=1;child.mask.emplace();MaskEffect generator;generator.kind=MaskEffectKind::Generator;generator.generator=8;generator.optional_inputs=true;child.mask->effects.push_back(generator);preset->set.layers={child,group};preset->set.selected_layer=group.id;smart.material_preset=preset;smart.bytes=write_material_preset(*preset);catalog.add(smart);
 StudioAsset texture;texture.name="Drop test · Texture";texture.filename="drop-texture.png";texture.usages={"texture"};catalog.add(texture);
 TextureSet set;set.uniform_editable=true;set.name="Drop destination";TextureLayer base,top;base.kind=top.kind=LayerKind::Fill;base.name="Base";top.name="Top";set.layers={base,top};set.selected_layer=top.id;NewProjectOptions options;BrushSettings brush;
 int drops=0,double_clicks=0,texture_drops=0;QUuid last_added,texture_effect;WorkspaceShell *window=nullptr;
 WorkspaceShell::Callbacks callbacks;
 callbacks.asset_dropped=[&](const StudioAsset &asset,QUuid target,bool above,bool){
  if(asset.usages.contains("texture")){assert(target==top.id&&set.selected_effect==texture_effect);++texture_drops;return;}
  ++drops;set.selected_layer=target;
  if(asset.material_preset)last_added=apply_material_preset(set,*asset.material_preset);
  else {TextureDocument::add_fill(set);last_added=set.selected_layer;TextureDocument::layer(set,last_added).name=asset.name.toStdString();}
  TextureDocument::move_layer_relative(set,last_added,target,above);
  window->show_texture_set(&set,&options,&brush);
 };
 callbacks.effect_selected=[&](QUuid layer,QUuid effect){set.selected_layer=layer;set.selected_mask=true;set.selected_effect=effect;};
 callbacks.asset_selected=[&](const StudioAsset &asset){assert(asset.id==material.id);++double_clicks;};
 auto *placeholder=new QLabel("CPU regression fixture\n3D rendering is not initialized");placeholder->setAlignment(Qt::AlignCenter);
 WorkspaceShell shell(placeholder,std::move(callbacks));window=&shell;shell.set_asset_catalog(&catalog);shell.resize(1440,1000);shell.show();shell.reset_reference_layout();shell.show_texture_set(&set,&options,&brush);QApplication::processEvents();
 // Count bundled and user library materials independently of editor capacity.
 const auto builtin_count=builtin_polyhaven_materials().size();
 const auto label_for=[](std::size_t n){return n==1?QString("1 library material"):QString("%1 library materials").arg(n);};
 auto *count=shell.findChild<QLabel *>("layerMaterialLibraryCount");assert(count&&!count->isHidden()&&count->text()==label_for(builtin_count+1));
 AssetCatalog empty_catalog(library.filePath("empty"));shell.set_asset_catalog(&empty_catalog);assert(count->isHidden()==(builtin_count==0));assert(count->text()==label_for(builtin_count));
 empty_catalog.add(material);shell.refresh_alpha_shelf();assert(!count->isHidden()&&count->text()==label_for(builtin_count+1));
 StudioAsset second=material;second.id=QUuid::createUuid();empty_catalog.add(second);shell.refresh_alpha_shelf();assert(count->text()==label_for(builtin_count+2));
 shell.set_asset_catalog(nullptr);assert(count->isHidden());shell.set_asset_catalog(&catalog);
 auto *shelf=shell.findChild<QListWidget *>("shelfAssets");auto *layers=dynamic_cast<LayerStackList *>(shell.findChild<QListWidget *>("layerStack"));assert(shelf&&layers);QListWidgetItem *material_item=nullptr;for(int i=0;i<shelf->count();++i)if(QUuid(shelf->item(i)->data(Qt::UserRole).toString())==material.id)material_item=shelf->item(i);assert(material_item&&(material_item->flags()&Qt::ItemIsDragEnabled));
 std::unique_ptr<QMimeData> mime(shelf->model()->mimeData({shelf->indexFromItem(material_item)}));assert(mime->data("application/x-bts-asset")==material.id.toString().toUtf8());
 QDir().mkpath(directory);
 const auto capture=[&](const QString &name){QApplication::processEvents();assert(shell.grab().save(directory+"/"+name+".png"));};
 const auto drop=[&](int gap,QMimeData &data,bool capture_gap){
  const QPoint point(120,gap==layers->count()?layers->visualItemRect(layers->item(gap-1)).bottom()+20:layers->visualItemRect(layers->item(gap)).top()+2);
  QDragEnterEvent enter(point,Qt::CopyAction,&data,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(layers->viewport(),&enter);assert(enter.isAccepted()&&layers->insertion_row()>=gap);
  if(capture_gap)capture("06-workspace-insertion");
  QDropEvent drop(point,Qt::CopyAction,&data,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(layers->viewport(),&drop);assert(drop.isAccepted());QApplication::processEvents();
 };
 drop(1,*mime,true);assert(drops==1&&set.layers[1].id==last_added&&set.layers[0].id==base.id&&set.layers[2].id==top.id);
 drop(0,*mime,false);assert(drops==2&&set.layers.back().id==last_added);
 drop(layers->count(),*mime,false);assert(drops==3&&set.layers.front().id==last_added);
 QMimeData smart_mime;smart_mime.setData("application/x-bts-asset",smart.id.toString().toUtf8());drop(1,smart_mime,false);assert(drops==4&&TextureDocument::layer(set,last_added).kind==LayerKind::Folder);TextureDocument::validate_hierarchy(set);capture("07-workspace-applied");
 // A texture dropped on a mask effect still targets that effect, not a new layer.
 auto &masked=TextureDocument::layer(set,top.id);masked.mask=LayerMask{};MaskEffect fill;fill.kind=MaskEffectKind::Fill;texture_effect=fill.id;masked.mask->effects.push_back(fill);set.selected_layer=top.id;set.selected_mask=true;set.selected_effect=fill.id;shell.show_texture_set(&set,&options,&brush);QApplication::processEvents();
 int effect_row=-1;for(int i=0;i<layers->count();++i)if(QUuid(layers->item(i)->data(Qt::UserRole+10).toString())==fill.id)effect_row=i;assert(effect_row>=0);
 QMimeData texture_mime;texture_mime.setData("application/x-bts-asset",texture.id.toString().toUtf8());drop(effect_row,texture_mime,false);assert(texture_drops==1);set.selected_mask=false;set.selected_effect=QUuid();
 // Real Shelf dispatch remains wired; catalog refresh may have replaced items.
 for(int i=0;i<shelf->count();++i)if(QUuid(shelf->item(i)->data(Qt::UserRole).toString())==material.id){Q_EMIT shelf->itemDoubleClicked(shelf->item(i));break;}assert(double_clicks==1);
 // Shared geometry publication used by viewport drops, within one candidate.
 auto previous=set;TextureDocument::add_fill(set);const auto added=set.selected_layer;constrain_asset_insertion(set,previous,"Right mesh",set.layers.back().id,true);assert(TextureDocument::layer(set,added).geometry.includes("Right mesh"));assert(!TextureDocument::layer(set,added).geometry.includes("Left mesh"));assert(TextureDocument::layer(set,base.id).geometry.includes("Left mesh"));
 auto *progress=new AssetApplyProgress(&shell);shell.statusBar()->addPermanentWidget(progress);assert(progress->begin(material.name));progress->stage("Evaluate channels",1);capture("08-workspace-progress");progress->completed=[&](const QString &error,bool success){if(!success)shell.set_error_status(error);};progress->fail("Invalid source graph (regression fixture)");assert(!progress->active()&&!QApplication::overrideCursor());capture("09-workspace-error");
 // This CPU-only entry point returns before BlenderHost shutdown. Join the
 // shelf worker while QApplication and its font/image services still exist.
 stop_asset_thumbnail_jobs();
 std::puts("Workspace asset drop: actual Shelf MIME, top/middle/bottom insertion, smart folder, double-click, geometry constraint and visible progress/error PASS");return 0;
}
