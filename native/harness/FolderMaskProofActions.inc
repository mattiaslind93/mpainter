// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit diagnostic fixture; normal Folder edits still use the production callbacks.
    if(parser.isSet("gui-fix-demo")&&workspace&&qEnvironmentVariableIsSet("BTS_BK_FOLDER_MASK")){
      const auto directory=parser.value("gui-fix-demo");const auto project_directory=qEnvironmentVariable("BTS_BK_PROJECT_DIR",directory);
      const auto add=[&](const char *name,std::function<void()> run){auto *action=new QAction(workspace);action->setObjectName(name);QObject::connect(action,&QAction::triggered,workspace,std::move(run));};
      add("guiBkPrepare",[&]{edit_material([&](bts::TextureSet &set){
        bts::TextureLayer backdrop,child,folder;backdrop.kind=child.kind=bts::LayerKind::Fill;folder.kind=bts::LayerKind::Folder;
        backdrop.name="BK backdrop .8";child.name="BK Multiply child .5";folder.name="BK Folder";
        backdrop.values.base_color={.8,.8,.8,1};backdrop.values.roughness=.8;child.values.base_color={.5,.5,.5,1};child.values.roughness=.5;
        child.blending.fill(bts::LayerBlend::Multiply);folder.blending.fill(bts::LayerBlend::Passthrough);folder.opacity.fill(.6f);child.parent=folder.id;child.depth=1;
        set.layers={backdrop,child,folder};set.selected_layer=folder.id;set.selected_mask=false;set.selected_geometry=false;set.selected_effect=QUuid();set.channels={true,false,true,false,false};set.precision[0]=bts::ChannelPrecision::RGB32F;set.precision[2]=bts::ChannelPrecision::L32F;
      });viewport->set_layout({bts::ViewLayout::Split,false});viewport->focus_uv({.5,.5},1.2);brush.diameter=25;brush.spacing=5;brush.hardness=1;brush.flow=100;brush.stroke_opacity=100;brush.pressure_size=false;brush.pressure_flow=false;select_material(selected_material);workspace->inspector_splitter()->setSizes({100,430,440,0});});
      add("guiBkSave",[&,directory]{save_document(directory+"/folder-mask.blend");});
      add("guiBkOpen",[&,project_directory]{begin_open_document(project_directory+"/folder-mask.blend");});
      add("guiBkSaveBitmap",[&,directory]{save_document(directory+"/folder-bitmap.blend");});
      add("guiBkSavePaint",[&,directory]{save_document(directory+"/folder-paint.blend");});
      add("guiBkOpenPaint",[&,project_directory]{begin_open_document(project_directory+"/folder-paint.blend");});
      add("guiBkOpenBitmap",[&,project_directory]{begin_open_document(project_directory+"/folder-bitmap.blend");});
      add("guiBkUv",[&]{viewport->set_layout({bts::ViewLayout::Split,false});viewport->focus_uv({.5,.5},1.2);select_material(selected_material);});
      add("guiBkAcknowledgeRefreshWarning",[&]{last_material_error.clear();refresh_document_ui(false,snapshot.materials[selected_material].session_id,false);});
      add("guiBkCyclesSample",[&]{workspace->setProperty("bkCyclesMirrors",host.diagnostic_native_source_queues()["bk_cycles_mirrors"].toObject());});
      add("guiBkSample",[&]{if(!document||selected_material<0)throw std::runtime_error("BK sample without project");host.flush_texture_updates();const auto &set=document->set(snapshot.materials[selected_material].session_id);QJsonArray probes;
        for(const auto xy:std::array<std::array<int,2>,3>{{{32,64},{96,64},{64,64}}}){QJsonObject sample{{"xy",QJsonArray{xy[0],xy[1]}}};for(const auto &[channel,key]:std::array<std::pair<int,const char *>,2>{{{2,"roughness"},{15,"mask"}}}){const auto pixel=host.diagnostic_gpu_pixel(set.material_session_uid,channel,xy[0],xy[1]);if(channel==15){sample["mask_probe_available"]=bool(pixel);sample["mask_probe_scope"]="Published mask inspection image only";if(!pixel&&host.inspection_mode()!=bts::InspectionMode::Mask)continue;}if(!pixel)throw std::runtime_error(channel==15?"BK isolated native mask GPU pixel absent":"BK authoritative Roughness GPU pixel absent");QJsonArray values;for(float value:*pixel)values.append(value);sample[key]=values;}probes.append(sample);}workspace->setProperty("bkSamples",probes);
      });
    }
