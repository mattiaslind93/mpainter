    if(parser.isSet("gui-fix-demo")&&workspace&&qEnvironmentVariableIsSet("BTS_CHECKBOX_DEMO")){
      const auto add=[&](const char *name,std::function<void()> fn){auto *action=new QAction(workspace);action->setObjectName(name);QObject::connect(action,&QAction::triggered,workspace,std::move(fn));};
      add("checkboxPrepare",[&]{
        if(!document||snapshot.materials.size()!=2)throw std::runtime_error("Checkbox fixture needs two materials");
        bts::PaintRecordingScope cpu(false);
        for(const auto &stored:document->texture_sets()){
          auto &set=document->set(stored.material_session_uid);set.texture_resolution=128;
          auto &layer=set.layers.front();layer.channels={true,false,false,false,false};
          bts::PaintPixels::Stamp stamp{};stamp.center={.5,.5};stamp.axis_x={.35,0};stamp.axis_y={0,.35};stamp.hardness=1;stamp.channels={true,false,false,false,false};stamp.values.base_color={.12f,.66f,.45f,1};
          layer.pixels.stamp_working(128,stamp,set.tiles);stamp.center={1.5,.5};layer.pixels.stamp_working(128,stamp,set.tiles);
          apply_preview(set,128);
        }
        viewport->set_layout({bts::ViewLayout::SceneOnly,false});refresh_document_ui(false);
      });
      add("checkboxProbe",[&]{
        const auto resident=bts::history_mapping_rss();
        QFile process("/proc/self/status");qint64 rss=0;if(process.open(QIODevice::ReadOnly))for(const auto &line:process.readAll().split('\n'))if(line.startsWith("VmRSS:"))rss=line.simplified().split(' ')[1].toLongLong()*1024;
        QJsonArray hashes,samples,loaded;
        for(const auto &set:document->texture_sets()){
          QJsonArray tiles;for(int tile:set.tiles){tiles.append(QJsonArray{tile,set.tile_loaded(tile)});for(const auto &layer:set.layers)hashes.append(QString::number(layer.pixels.tile(tile).hash(0),16));
            QJsonArray rgba;if(set.tile_loaded(tile))if(const auto pixel=host.diagnostic_gpu_pixel(set.material_session_uid,0,64,64,tile))for(float v:*pixel)rgba.append(v);samples.append(rgba);
          }
          loaded.append(QJsonObject{{"loaded",set.loaded},{"tiles",tiles}});
        }
        std::uint64_t composites=0;for(const auto &[name,group]:blender::gpu::host_texture_memory::snapshot())if(name.starts_with("BTS Composite "))composites+=group.bytes;
        workspace->setProperty("checkboxProbe",QJsonObject{{"hashes",hashes},{"samples",samples},{"loaded",loaded},{"gpu_bytes",qint64(composites)},{"gpu_total_bytes",qint64(blender::gpu::host_texture_memory::live_bytes())},{"cpu_paint_bytes",qint64(resident)},{"process_rss_bytes",rss}});
      });
      add("checkboxSave",[&]{save_document(parser.value("gui-fix-demo")+"/checkbox.blend");});
      add("checkboxOpen",[&]{open_document(parser.value("gui-fix-demo")+"/checkbox.blend");});
      add("checkboxCycles",[&]{host.set_render_quality(4,false);host.set_render_preview(true,true);viewport->update();});
      add("checkboxEevee",[&]{host.set_render_preview(false);viewport->update();});
      add("checkboxExport",[&]{std::vector<QUuid> ids;std::size_t expected=0;for(const auto &set:document->texture_sets()){ids.push_back(set.id);expected+=set.tiles.size();}const auto source=host.export_source(*document,ids);if(source.size()!=expected)throw std::runtime_error("Export omitted unloaded tiles");for(const auto &tile:source)if(tile.resolution!=128)throw std::runtime_error("Export used a residency placeholder");});
    }
