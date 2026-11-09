// SPDX-License-Identifier: GPL-3.0-or-later
// Workspace-owner callbacks, included once in main beside texture-loader setup.
// Workers receive only copied immutable assets/descriptions, never these locals.
const auto initialize_native_image_ui=[&]{
 bts::NativeImageUiHooks hooks;
 hooks.target=[&]()->std::optional<bts::NativeImageUiTarget>{
  if(!document||selected_material<0||(render_controller&&render_controller->active()))return {};
  const auto uid=snapshot.materials[selected_material].session_id;auto &set=document->set(uid);
  if(set.selected_mask||set.selected_geometry||!set.selected_effect.isNull()||set.selected_layer.isNull())return {};
  const auto &layer=bts::TextureDocument::layer(set,set.selected_layer);
  if(layer.kind!=bts::LayerKind::Fill||layer.instance||!layer.fill.native_material)return {};
  const auto source_id=QUuid(QString::fromStdString(layer.fill.material_resource));
  const auto found=std::find_if(assets.assets().begin(),assets.assets().end(),[&](const auto &a){return a.id==source_id;});
  if(found==assets.assets().end()||!bts::is_native_material_asset(*found))throw std::runtime_error("Native material source is missing from the resource catalog");

  bts::NativeImageUiTarget target;target.document=document->id();target.layer=layer.id;target.material_uid=uid;target.source=*found;target.resources=assets.assets();target.instance=*layer.fill.native_material;
  const auto project=target.document,layer_id=target.layer,set_id=set.id;const auto revision=document_revision;const auto hash=found->hash();const auto authored=layer.fill.native_material;const auto source=layer.fill.material_resource;
  target.current=[&,project,uid,layer_id,set_id,revision,source_id,hash,authored,source]{
   if(!document||document->id()!=project||document_revision!=revision||selected_material<0||snapshot.materials[selected_material].session_id!=uid||(render_controller&&render_controller->active()))return false;
   try{auto &set=document->set(uid);if(set.id!=set_id||set.selected_layer!=layer_id||set.selected_mask||set.selected_geometry||!set.selected_effect.isNull())return false;const auto &layer=bts::TextureDocument::layer(set,layer_id);if(layer.kind!=bts::LayerKind::Fill||layer.instance||layer.fill.material_resource!=source||layer.fill.native_material!=authored)return false;const auto asset=std::find_if(assets.assets().begin(),assets.assets().end(),[&](const auto &a){return a.id==source_id;});return asset!=assets.assets().end()&&asset->hash()==hash;}catch(const std::exception &){return false;}
  };
  return target;
 };
 hooks.publish=[&](bts::NativeImageChange change,const QUuid &staging,const bts::NativeImageUiTarget &target,bool reset){
  if(!target.current())throw std::runtime_error("Native image target changed during preparation");
  host.validate_native_material_target(staging,document->set(target.material_uid));
  static_assert(std::is_nothrow_swappable_v<bts::AssetCatalog>);
  bts::AssetCatalog candidate=assets;
  const auto add=[&](const bts::StudioAsset &asset){const auto found=std::find_if(candidate.assets().begin(),candidate.assets().end(),[&](const auto &a){return a.id==asset.id;});if(found==candidate.assets().end())candidate.add(asset);else if(found->hash()!=asset.hash()||found->usages!=asset.usages)throw std::runtime_error("Native image resource identity changed during loading");};
  for(const auto &texture:change.prepared.textures)add(texture);add(change.prepared.asset);
  const auto retained=candidate.retain_project(change.prepared.asset);const auto old_catalog_id=std::any_of(assets.assets().begin(),assets.assets().end(),[&](const auto &a){return a.id==retained;});
  const bool alias=!host.has_native_material(retained);bool published=false;
  const auto discard=qScopeGuard([&]{if(!published&&alias&&!old_catalog_id)try{host.discard_native_material_candidate(retained);}catch(const std::exception &e){std::fprintf(stderr,"BTS_NATIVE_IMAGE_DISCARD_ERROR %s\n",e.what());}});
  auto authored=std::make_shared<const bts::NativeMaterialDescription>(std::move(change.instance));authored->validate();
  const auto source=retained.toString(QUuid::WithoutBraces).toStdString();if(alias)host.alias_native_material(staging,retained);
  if(!target.current())throw std::runtime_error("Native image target changed before publication");
  const auto *before=document.get();
  edit_texture_set(target.material_uid,[authored,source,id=target.layer](bts::TextureSet &set){auto &layer=bts::TextureDocument::layer(set,id);if(layer.kind!=bts::LayerKind::Fill||layer.instance||!layer.fill.native_material)throw std::runtime_error("Native image fill changed");layer.fill.native_material=authored;layer.fill.material_resource=source;},[&]()noexcept{std::swap(assets,candidate);published=true;});
  if(!published)throw std::runtime_error((last_material_error.isEmpty()?QString("Native image edit did not change its source binding"):last_material_error).toStdString());
  // After the noexcept publication/document swaps, errors describe UI refresh,
  // not a failed transaction. Never roll back its now-published catalog alone.
  try{
   if(document.get()!=before&&!undo_history.empty())undo_history.back().label=reset?QStringLiteral("Reset native images"):QStringLiteral("Replace native image");
   if(!last_material_error.isEmpty())workspace->append_console("Native image edit committed; UI refresh warning: "+last_material_error);
   workspace->refresh_alpha_shelf();refresh_history_actions();viewport->update();
  }catch(const std::exception &e){std::fprintf(stderr,"BTS_NATIVE_IMAGE_COMMITTED_REFRESH_WARNING %s\n",e.what());}
 };
 hooks.shelf=[&]{workspace->show_texture_shelf();};hooks.redraw=[&]{viewport->update();};
 hooks.status=[&](const QString &text){workspace->set_status(text);workspace->setProperty("nativeImageStatus",text);workspace->setProperty("nativeImageError",QString());};
 hooks.error=[&](const QString &text){workspace->set_error_status("Native image: "+text);workspace->append_console("Native image: "+text);workspace->setProperty("nativeImageError",text);};
 native_image_ui=std::make_unique<bts::NativeMaterialImageUi>(workspace,workspace->native_image_panel(),host,std::move(hooks));
};
