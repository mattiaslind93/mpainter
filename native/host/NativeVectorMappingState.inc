// Explicit AW test metadata. No GPU probes during edits or ordinary snapshots.
      if(qEnvironmentVariableIsSet("BTS_AW_VECTOR_UI")||qEnvironmentVariableIsSet("BTS_AW_REAL_UI")||qEnvironmentVariableIsSet("BTS_NATIVE_WORLD_NORMAL_UI")){
        state["redo_count"]=int(redo_history.size());state["document_path"]=document_path;
        state["open_pending"]=opening_project||project_opener.pending();
        state["save_pending"]=bool(active_save)||bool(requested_save)||host.save_project_pending();
        state["native_pending"]=native_material_loader&&native_material_loader->pending();
        state["import_pending"]=workspace->property("blendMaterialImporting").toBool();
        state["open_error"]=workspace->property("projectOpenError").toString();state["save_error"]=workspace->property("projectSaveError").toString();state["material_error"]=last_material_error;
        if(auto *error=workspace->findChild<QLabel *>("workspaceErrorText");error&&error->isVisible())state["workspace_error"]=error->text();
        state["layer_thumbnails_pending"]=workspace->layer_thumbnails_pending();
        state["cpu_materializations"]=qint64(bts::PaintPixels::recipe_materializations());state["cpu_layer_compositions"]=qint64(bts::local_paint_materializations);
        state["compiled_shaders"]=qint64(blender::GPU_pass_global_compilation_count());state["inspection"]=int(host.inspection_mode());
        const auto queues=host.diagnostic_native_source_queues();state["queues"]=queues;
        if(document&&selected_material>=0){
          if(qEnvironmentVariableIsSet("BTS_AW_REAL_UI"))state["real_color_context"]=host.color_state();
          bts::PaintMetadataScope paint_metadata;bts::EditStateScope metadata_scope;
          const auto &set=document->set(snapshot.materials[selected_material].session_id);
          if(qEnvironmentVariableIsSet("BTS_NATIVE_WORLD_NORMAL_UI")){
            auto world=host.native_world_normal_status(set.material_session_uid);
            if(workspace->property("nativeWorldNormalProbe").toBool()&&!host.texture_updates_pending()){
              workspace->setProperty("nativeWorldNormalProbe",false);const bool edge=workspace->property("nativeWorldNormalEdgeProbe").toBool();workspace->setProperty("nativeWorldNormalEdgeProbe",false);workspace->setProperty("nativeWorldNormalSamples",host.diagnostic_native_world_normal_samples(set.material_session_uid));if(edge)workspace->setProperty("nativeWorldNormalEdgeSamples",host.diagnostic_native_world_normal_samples(set.material_session_uid,true));workspace->setProperty("nativeWorldNormalProbeSerial",workspace->property("nativeWorldNormalProbeSerial").toULongLong()+1);
            }
            world["samples"]=workspace->property("nativeWorldNormalSamples").toJsonArray();world["edge_samples"]=workspace->property("nativeWorldNormalEdgeSamples").toJsonArray();world["probe_serial"]=QString::number(workspace->property("nativeWorldNormalProbeSerial").toULongLong());state["native_world_normal"]=world;
          }
          state["set"]=set.id.toString(QUuid::WithoutBraces);state["layer_id"]=set.selected_layer.toString(QUuid::WithoutBraces);
          if(qEnvironmentVariableIsSet("BTS_AW_REAL_UI")){
            state["real_bake_configuration"]=document->bake_configuration();QJsonArray maps;
            for(int role=0;role<7;++role)if(set.mesh_maps[role]){QJsonArray tiles;for(const auto &[tile,plane]:set.mesh_maps[role]->tiles)tiles.append(QJsonObject{{"tile",tile},{"width",plane.resolution},{"height",plane.rows()},{"hash",QString::fromStdString(plane.hash)},{"pixels_present",bool(plane.pixels)},{"storage_hash",plane.pixels?QString::fromStdString(plane.pixels->hash):QString()}});maps.append(QJsonObject{{"role",role},{"name",QString::fromStdString(set.mesh_maps[role]->name)},{"tiles",tiles}});}state["real_mesh_maps"]=maps;
          }
          state["tiles"]=QJsonArray::fromVariantList([&]{QVariantList values;for(int tile:set.tiles)values.append(tile);return values;}());state["width"]=set.width(1001);state["height"]=set.rows(1001);
          const auto metadata=document->edit_state();QJsonArray lower;
          for(const auto &entry:metadata["texture_sets"].toArray())if(QUuid(entry.toObject()["id"].toString())==set.id)for(const auto &row:entry.toObject()["layers"].toArray()){
            if(QUuid(row.toObject()["id"].toString())==set.selected_layer)state["layer"]=row;else lower.append(row);
          }
          state["lower"]=lower;
          for(const auto &asset:assets.assets())if(asset.id==QUuid(state["layer"].toObject()["fill_projection"].toObject()["material_resource"].toString())){
            state["source_id"]=asset.id.toString(QUuid::WithoutBraces);state["source_sha256"]=QString::fromStdString(bts::native_material_source_hash(asset.bytes));
            if(asset.fill_preset&&asset.fill_preset->native_material)state["source_description"]=bts::native_material_description_json(*asset.fill_preset->native_material);
          }
          const auto binding=queues["bindings"].toObject()[QString::number(set.material_session_uid)].toObject();state["binding"]=binding;
          bool ready=!binding.isEmpty();const auto handles=binding["handles"].toArray(),waiting=binding["waiting"].toArray();
          for(int channel:{0,2}){const auto row=handles.size()>channel?handles[channel].toArray():QJsonArray{};ready&=waiting.size()>channel&&!waiting[channel].toBool()&&row.size()==3&&(row[1].toString()!="0"||row[2].toString()!="0");}
          state["probe_ready"]=ready;
          if(workspace->property("awVectorProbe").toBool()&&ready&&!host.texture_updates_pending()){
            QJsonArray packets;
            if(qEnvironmentVariableIsSet("BTS_AW_REAL_UI")){
              std::vector<std::array<int,2>> points;points.reserve(256);for(int y=0;y<16;++y)for(int x=0;x<16;++x)points.push_back({4+8*x,4+8*y});
              for(int channel:{0,2}){const auto values=host.diagnostic_gpu_pixels(set.material_session_uid,channel,points,1001);QJsonArray samples;for(const auto &value:values){QJsonArray rgba;for(float x:value)rgba.append(x);samples.append(rgba);}packets.append(QJsonObject{{"tile",1001},{"channel",channel},{"values",samples}});}
              state["probe_grid"]=QJsonObject{{"tile",1001},{"origin",QJsonArray{4,4}},{"step",QJsonArray{8,8}},{"shape",QJsonArray{16,16}},{"samples_per_channel",256},{"order","row-major y then x"},{"scope","Explicit settled diagnostic only; one256point batch per channel, no per-edit/frame readback"}};
            }else{
            const std::array<std::array<int,2>,3> points{{{32,64},{64,64},{96,64}}};
            for(int channel:{0,2}){const auto values=host.diagnostic_gpu_pixels(set.material_session_uid,channel,points,1001);QJsonArray samples;for(const auto &value:values){QJsonArray rgba;for(float x:value)rgba.append(x);samples.append(rgba);}packets.append(QJsonObject{{"tile",1001},{"channel",channel},{"values",samples}});}
            }
            state["gpu"]=packets;
          }
        }
        return state;
      }
