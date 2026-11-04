      // Read-only state for the explicit BM actor. Pixel diagnostics are never
      // requested by its pointer handler or by ordinary per-frame snapshots.
      if(qEnvironmentVariableIsSet("BTS_BM_ADJUSTMENT_DEMO")){
        state["redo_count"]=int(redo_history.size());
        const auto bm_queues=host.diagnostic_native_source_queues();
        state["bm_cycles_mirrors"]=bm_queues["bm_cycles_mirrors"];
        state["bm_inspection"]=int(host.inspection_mode());
        state["bm_document_path"]=document_path;
        state["open_pending"]=project_opener.pending();
        state["save_pending"]=bool(active_save)||bool(requested_save)||host.save_project_pending();
        state["open_error"]=workspace->property("projectOpenError").toString();
        state["save_error"]=workspace->property("projectSaveError").toString();
        state["material_error"]=last_material_error;
        state["layer_thumbnails_pending"]=workspace->layer_thumbnails_pending();
        state["cpu_materializations"]=qint64(bts::PaintPixels::recipe_materializations());
        state["cpu_layer_compositions"]=qint64(bts::local_paint_materializations);
        state["bm_transfers"]=bts::gpu_transfer_snapshot();
        state["bm_clone_source"]=bool(viewport->clone_source);
        state["bm_refusal"]=workspace->property("bmRefusal").toJsonObject();
        QJsonArray ink;for(float x:brush.values.base_color)ink.append(x);
        state["bm_brush"]=QJsonObject{{"smudge",brush.smudge},{"clone",brush.clone},{"erase",brush.erase},{"diameter",brush.diameter},{"flow",brush.flow},{"spacing",brush.spacing},{"pressure_size",brush.pressure_size},{"ink",ink}};
        if(auto *error=workspace->findChild<QLabel *>("workspaceErrorText");error&&error->isVisible())state["workspace_error"]=error->text();
        if(document&&selected_material>=0){
          bts::PaintMetadataScope paint_metadata;bts::EditStateScope metadata;
          const auto &set=document->set(snapshot.materials[selected_material].session_id);
          const auto document_metadata=document->edit_state();state["document"]=document_metadata;
          state["bm_runtime_uid"]=QString::number(set.material_session_uid);
          // Existing owner metadata only: no image allocation or GPU readback.
          const auto bm_binding=bm_queues["bindings"].toObject()[QString::number(set.material_session_uid)].toObject();
          state["bm_binding_readiness"]=bm_binding;
          bool bm_probe_ready=!bm_binding.isEmpty();const auto bm_handles=bm_binding["handles"].toArray(),bm_waiting=bm_binding["waiting"].toArray();
          for(int channel:{0,2}){const auto handles=bm_handles.size()>channel?bm_handles[channel].toArray():QJsonArray{};bm_probe_ready&=bm_waiting.size()>channel&&!bm_waiting[channel].toBool()&&handles.size()==3&&(handles[1].toString()!="0"||handles[2].toString()!="0");}
          state["bm_probe_ready"]=bm_probe_ready;

          state["bm_set"]=set.id.toString(QUuid::WithoutBraces);state["bm_layer_id"]=set.selected_layer.toString(QUuid::WithoutBraces);
          state["bm_selected_mask"]=set.selected_mask;state["bm_selected_effect"]=set.selected_effect.toString(QUuid::WithoutBraces);
          QJsonArray tiles,points,dimensions;for(int tile:set.tiles){tiles.append(tile);dimensions.append(QJsonObject{{"tile",tile},{"width",set.width(tile)},{"height",set.rows(tile)}});for(double u:{.25,.5,.75}){const auto point=viewport->uv_point({double((tile-1001)%10)+u,double((tile-1001)/10)+.5});points.append(QJsonArray{point.x(),point.y()});}}
          state["bm_tiles"]=tiles;state["bm_points"]=points;state["bm_dimensions"]=dimensions;
          // Geometry-only correspondence for the emission-frame witness. These
          // are actual imported triangle positions, not a pixel/source oracle.
          QJsonArray surface_points;
          for(int tile:set.tiles)for(double u:{.25,.5,.65,.75}){
            const int x=std::min(set.width(tile)-1,int(u*set.width(tile))),y=set.rows(tile)/2;
            const double target_u=(tile-1001)%10+(x+.5)/set.width(tile),target_v=(tile-1001)/10+(y+.5)/set.rows(tile);
            bool found=false;
            for(const auto &triangle:snapshot.uv_triangles){
              if(snapshot.materials[triangle.material].session_id!=set.material_session_uid)continue;
              const auto &a=triangle.corners[0],&b=triangle.corners[1],&c=triangle.corners[2];const double det=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1]);if(std::abs(det)<1e-15)continue;
              const double wa=((b[1]-c[1])*(target_u-c[0])+(c[0]-b[0])*(target_v-c[1]))/det,wb=((c[1]-a[1])*(target_u-c[0])+(a[0]-c[0])*(target_v-c[1]))/det,wc=1-wa-wb;
              if(std::min({wa,wb,wc}) < -1e-8)continue;
              QVector3D world;for(int axis=0;axis<3;++axis)world[axis]=float(wa*triangle.positions[0][axis]+wb*triangle.positions[1][axis]+wc*triangle.positions[2][axis]);const auto point=viewport->scene_point(world);
              surface_points.append(QJsonObject{{"tile",tile},{"xy",QJsonArray{x,y}},{"world",QJsonArray{world.x(),world.y(),world.z()}},{"screen",QJsonArray{point.x(),point.y()}},{"triangle",qint64(triangle.triangle_id)}});found=true;break;
            }
            if(!found)throw std::runtime_error("BM diagnostic UV sample has no imported surface triangle");
          }
          state["bm_cycles_surface_points"]=surface_points;
          QJsonArray lower;bool found=false;
          for(const auto &entry:document_metadata["texture_sets"].toArray()){
            const auto value=entry.toObject();if(QUuid(value["id"].toString())!=set.id)continue;
            for(const auto &row:value["layers"].toArray()){
              const auto layer=row.toObject();if(QUuid(layer["id"].toString())==set.selected_layer){state["bm_layer"]=layer;found=true;break;}lower.append(layer);
            }
          }
          state["bm_lower"]=lower;state["bm_layer_found"]=found;
          if(workspace->property("bmProbe").toBool()&&!host.texture_updates_pending()&&bm_probe_ready){
            QJsonArray probes;
            for(int tile:set.tiles)for(int channel:{0,2}){
              std::array<std::array<int,2>,4> locations;int i=0;for(double u:{.25,.5,.65,.75})locations[i++]={std::min(set.width(tile)-1,int(u*set.width(tile))),set.rows(tile)/2};
              const auto values=host.diagnostic_gpu_pixels(set.material_session_uid,channel,locations,tile);QJsonArray samples;
              for(const auto &value:values){QJsonArray rgba;for(float x:value)rgba.append(x);samples.append(rgba);}
              QJsonArray points;for(std::size_t i=0;i<std::min(values.size(),locations.size());++i)points.append(QJsonObject{{"xy",QJsonArray{locations[i][0],locations[i][1]}},{"rgba",samples[int(i)]}});
              probes.append(QJsonObject{{"tile",tile},{"channel",channel},{"values",samples},{"points",points}});
            }
            state["bm_probes"]=probes;
          }
        }
        return state;
      }
