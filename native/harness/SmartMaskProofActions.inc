// Test-only fixture and metadata hooks. Apply/edit/Bake/Undo/persistence remain
// the production callbacks; no CPU mask stack runs from this UI diagnostic.
    if(parser.isSet("gui-fix-demo")&&workspace&&qEnvironmentVariableIsSet("BTS_BN_SMART_MASK")){
      const auto directory=parser.value("gui-fix-demo");const auto project=directory+"/smart-masks.blend";
      const auto add=[&](const char *name,std::function<void()> run){auto *action=new QAction(workspace);action->setObjectName(name);QObject::connect(action,&QAction::triggered,workspace,std::move(run));};
      add("guiBnTarget",[&]{edit_material([&](bts::TextureSet &set){
        const bool folder=workspace->property("bnTargetFolder").toBool();bts::TextureLayer target;target.kind=folder?bts::LayerKind::Folder:bts::LayerKind::Fill;
        target.name="BN "+workspace->property("bnTargetName").toString().toStdString()+(folder?" [Folder]":" [Fill]");target.values.base_color={1,1,1,1};
        if(folder){bts::TextureLayer child;child.kind=bts::LayerKind::Fill;child.name="BN white child";child.parent=target.id;child.depth=1;child.values.base_color={1,1,1,1};set.layers.push_back(std::move(child));}
        set.selected_layer=target.id;set.layers.push_back(std::move(target));set.selected_mask=false;set.selected_effect=QUuid();set.channels={true,false,true,false,false};set.precision[0]=bts::ChannelPrecision::RGB32F;set.precision[2]=bts::ChannelPrecision::L32F;
      });viewport->set_layout({bts::ViewLayout::Split,false});viewport->focus_uv({.5,.5},1.2);select_material(selected_material);workspace->inspector_splitter()->setSizes({100,350,440,0});});
      add("guiBnSnapshot",[&]{
        QJsonObject value{{"cpu_materializations",qint64(bts::PaintPixels::recipe_materializations())},{"cpu_layer_compositions",qint64(bts::local_paint_materializations)}};if(!document||selected_material<0){workspace->setProperty("bnState",value);return;}const auto &set=document->set(snapshot.materials[selected_material].session_id);const auto found=std::find_if(set.layers.begin(),set.layers.end(),[&](const auto &candidate){return candidate.id==set.selected_layer;});if(found==set.layers.end())throw std::runtime_error("BN selected layer missing");const auto &layer=*found;
        bts::EditStateScope metadata;value["document"]=document->to_json();value["selected_layer"]=layer.id.toString();value["folder"]=layer.kind==bts::LayerKind::Folder;value["has_mask"]=layer.mask.has_value();int baked=0;for(int role:{1,3,4,5,6})baked+=set.mesh_maps[role].has_value();value["baked_roles"]=baked;
        if(layer.mask){const auto effects=bts::mask_effects_json(layer.mask->effects);value["effects"]=effects;value["mask_state"]=QJsonObject{{"background",layer.mask->background},{"enabled",layer.mask->enabled},{"effects",effects}};bool spatial=false,bitmap=false;
          for(const auto &effect:bts::mask_effects_recursive(layer.mask->effects)){spatial|=effect.kind==bts::MaskEffectKind::Generator&&effect.triplanar_grunge&&effect.surface_fallback;bitmap|=effect.kind==bts::MaskEffectKind::Fill&&effect.mapping.projection.mode==1&&effect.mapping.projection.texture&&bool(effect.mapping.projection.texture->pixels);}value["generator_surface_flags"]=spatial;value["triplanar_bitmap"]=bitmap;}
        workspace->setProperty("bnState",value);
      });
      add("guiBnSave",[&,project]{
        if(!document)throw std::runtime_error("BN Save without project");std::vector<QUuid> targets;for(const auto &set:document->texture_sets())for(const auto &layer:set.layers)if(layer.name.starts_with("BN ")&&layer.name!="BN white child")targets.push_back(layer.id);
        const auto expected=bts::smart_mask_proof::witness(*document,targets);QFile file(project+".smart-mask-witness.json");if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write BN pre-Save witness");file.write(QJsonDocument(expected).toJson());file.close();workspace->setProperty("bnExpected",expected);save_document(project);
      });
      add("guiBnOpen",[&,project]{begin_open_document(project);});
      add("guiBnValidateOpen",[&]{if(!document)throw std::runtime_error("BN Open without project");workspace->setProperty("bnOpenProof",bts::smart_mask_proof::validate(*document,workspace->property("bnExpected").toJsonObject()));});
      add("guiBnPrimeThumbnails",[&]{for(const auto &asset:bts::builtin_smart_mask_assets())if(asset.smart_mask)bts::asset_thumbnail(asset);});
      add("guiBnCheckThumbnails",[&]{
        const auto packets=workspace->property("bnThumbnailPackets").toJsonObject();std::set<QString> hashes;
        for(const char *name:{"Rust","Edge Wear","Crease Dirt","Moss","Ground Dirt","Scratches","Leaks & Stains","Surface Dust"}){const auto hash=packets[QString::fromLatin1(name)].toString();if(hash.isEmpty()){workspace->setProperty("bnWaitingThumbnails",true);workspace->setProperty("bnThumbnailsValid",false);return;}hashes.insert(hash);}
        if(hashes.size()!=8)throw std::runtime_error("BN native sphere thumbnail recipes are not distinct");workspace->setProperty("bnThumbnailsValid",true);workspace->setProperty("bnWaitingThumbnails",false);
      });
    }
