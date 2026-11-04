// Explicit test-only metadata/fixture actions. Import, Shelf application, typed
// editing, Undo, Save and Open use the existing production callbacks.
    if(parser.isSet("gui-fix-demo")&&workspace&&qEnvironmentVariableIsSet("BTS_BN_SPMSK_UI")){
      const auto project=qEnvironmentVariable("BTS_BN_SPMSK_PROJECT",parser.value("gui-fix-demo")+"/imported-smart-masks.blend");
      const auto add=[&](const char *name,std::function<void()> run){auto *action=new QAction(workspace);action->setObjectName(name);QObject::connect(action,&QAction::triggered,workspace,std::move(run));};
      add("guiSpmskTarget",[&]{edit_material([&](bts::TextureSet &set){
        const bool folder=workspace->property("spmskTargetFolder").toBool();bts::TextureLayer target;target.kind=folder?bts::LayerKind::Folder:bts::LayerKind::Fill;
        target.name="BN SPMSK "+workspace->property("spmskTargetName").toString().toStdString()+(folder?" Folder":" Fill");target.values.base_color={1,1,1,1};
        if(folder){bts::TextureLayer child;child.kind=bts::LayerKind::Fill;child.name="BN SPMSK child";child.parent=target.id;child.depth=1;child.values.base_color={1,1,1,1};set.layers.push_back(std::move(child));}
        set.selected_layer=target.id;set.layers.push_back(std::move(target));set.selected_mask=false;set.selected_geometry=false;set.selected_effect={};set.channels={true,false,true,false,false};set.precision[0]=bts::ChannelPrecision::RGB32F;set.precision[2]=bts::ChannelPrecision::L32F;
      });viewport->set_layout({bts::ViewLayout::Split,false});viewport->focus_uv({.5,.5},1.2);select_material(selected_material);workspace->inspector_splitter()->setSizes({100,350,440,0});});
      add("guiSpmskSnapshot",[&]{
        QJsonObject value{{"library",assets.directory()}};QJsonArray entries;
        for(const auto &asset:assets.assets())if(asset.scope==bts::AssetScope::User&&bts::is_spmsk_asset(asset)){
          const auto prepared=bts::prepared_spsm_source(asset);entries.append(QJsonObject{{"id",asset.id.toString()},{"name",asset.name},{"filename",asset.filename},{"source_sha256",asset.hash()},{"bytes",qint64(asset.bytes.size())},{"smart_mask_present",bool(asset.smart_mask)},{"smart_mask_pointer",QString::number(quintptr(asset.smart_mask.get()),16)},{"prepared_source_cached",bool(prepared)},{"prepared_source_pointer",QString::number(quintptr(prepared.get()),16)},{"prepared_cache_path",prepared?prepared->cache_path:QString{}},{"prepared_sha256",prepared?prepared->source_sha256:QString{}}});
        }value["user_sources"]=entries;
        if(document&&selected_material>=0){bts::EditStateScope metadata;const auto &set=document->set(snapshot.materials[selected_material].session_id);const auto found=std::find_if(set.layers.begin(),set.layers.end(),[&](const auto &candidate){return candidate.id==set.selected_layer;});if(found==set.layers.end())throw std::runtime_error("SPMSK selected layer missing");const auto &layer=*found;value["selected_layer"]=layer.id.toString();value["layer_count"]=int(set.layers.size());value["folder"]=layer.kind==bts::LayerKind::Folder;value["has_mask"]=bool(layer.mask);value["content_effects"]=bts::effects_json(layer.effects);if(layer.mask){value["effects"]=bts::mask_effects_json(layer.mask->effects);value["mask_state"]=QJsonObject{{"background",layer.mask->background},{"enabled",layer.mask->enabled},{"effects",bts::mask_effects_json(layer.mask->effects)}};}value["document"]=document->edit_state();}
        QJsonArray resources;for(const auto &asset:assets.project_assets())if(bts::is_spmsk_asset(asset))resources.append(QJsonObject{{"id",asset.id.toString()},{"filename",asset.filename},{"source_sha256",asset.hash()},{"bytes",qint64(asset.bytes.size())}});value["packed_sources"]=resources;
        workspace->setProperty("spmskState",value);
      });
      add("guiSpmskSave",[&,project]{if(!document)throw std::runtime_error("SPMSK Save without project");const auto expected=bts::spmsk_shelf_proof::witness(*document,assets.project_assets());QFile file(project+".spmsk-witness.json");if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write SPMSK persistence witness");file.write(QJsonDocument(expected).toJson());file.close();save_document(project);});
      add("guiSpmskOpen",[&,project]{begin_open_document(project);});
      add("guiSpmskValidateOpen",[&,project]{if(!document)throw std::runtime_error("SPMSK Open without project");QFile file(project+".spmsk-witness.json");if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read SPMSK persistence witness");const auto expected=QJsonDocument::fromJson(file.readAll()).object();workspace->setProperty("spmskOpenProof",bts::spmsk_shelf_proof::validate(*document,assets.project_assets(),expected));});
    }
