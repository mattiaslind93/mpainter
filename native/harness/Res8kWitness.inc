// Explicit low-resolution fixtures for the headless dropdown/shelf proof.
if (parser.isSet("gui-fix-demo") && workspace && qEnvironmentVariableIsSet("BTS_RES8K_DEMO")) {
  const auto add = [&](const char *name, std::function<void()> run) {
    auto *action = new QAction(workspace);
    action->setObjectName(name);
    QObject::connect(action, &QAction::triggered, workspace, std::move(run));
  };
  add("guiRes8kPrepareShelf", [&] {
    if (!document || selected_material < 0) throw std::runtime_error("Resolution proof requires a project");
    for (bool dynamic : {false, true}) {
      bts::SmartMaterialPreset preset{document->set(snapshot.materials[selected_material].session_id)};
      auto &set = preset.set;
      set.layers.clear();set.mesh_maps={};set.projection_geometry.reset();
      bts::TextureLayer folder;folder.kind=bts::LayerKind::Folder;
      folder.name=dynamic?"World height — saved folder":"Plain finish — saved folder";
      bts::TextureLayer fill;fill.kind=bts::LayerKind::Fill;fill.name="Turquoise base";
      fill.parent=folder.id;fill.depth=1;fill.channels={true,false,true,false,false};
      fill.values.base_color={.025f,.34f,.28f,1};fill.values.roughness=.45f;
      if(dynamic){fill.mask.emplace();bts::MaskEffect effect;effect.kind=bts::MaskEffectKind::Generator;effect.generator=1;effect.optional_inputs=true;effect.surface_fallback=true;fill.mask->effects.push_back(effect);}
      set.layers={fill,folder};set.selected_layer=folder.id;set.selected_effect={};set.selected_mask=false;
      bts::StudioAsset asset;asset.name=QString::fromStdString(folder.name);
      asset.filename=asset.id.toString(QUuid::WithoutBraces)+".btsmaterial";
      asset.usages={"smartmaterial"};asset.scope=bts::AssetScope::User;
      asset.bytes=bts::write_material_preset(preset);assets.add(std::move(asset));
      if(dynamic){
        edit_material([&](bts::TextureSet &target){target.layers={fill,folder};target.selected_layer=fill.id;target.selected_mask=true;target.selected_effect=fill.mask->effects.front().id;});
      }
    }
    viewport->set_layout({bts::ViewLayout::SceneOnly,false});
    select_material(selected_material);workspace->refresh_alpha_shelf();
    workspace->inspector_splitter()->setSizes({100,240,560,0});
  });
  add("guiRes8kGenerator", [&] {
    auto &set=document->set(snapshot.materials[selected_material].session_id);
    auto &fill=set.layers.front();set.selected_layer=fill.id;set.selected_mask=true;
    set.selected_effect=fill.mask->effects.front().id;
    select_material(selected_material);
  });
}
