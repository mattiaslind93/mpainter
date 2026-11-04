// Included beside other explicit actor actions in main.cc; dependencies belong
// in main's include list. No product mapping mutation is introduced here.
    if(parser.isSet("gui-fix-demo")&&workspace&&qEnvironmentVariableIsSet("BTS_BJ_PROJECTION_UI")){
      const auto add=[&](const char *name,std::function<void()> run){auto *a=new QAction(workspace);a->setObjectName(name);QObject::connect(a,&QAction::triggered,workspace,std::move(run));};
      add("guiBjSnapshot",[&]{
        QJsonObject value{{"document_path",document_path},{"mesh_objects",qint64(host.scene_snapshot().mesh_objects)},{"compiled_shaders",qint64(blender::GPU_pass_global_compilation_count())},
          {"cpu_materializations",qint64(bts::PaintPixels::recipe_materializations())},
          {"cpu_layer_compositions",qint64(bts::local_paint_materializations)},
          {"queues",host.diagnostic_native_source_queues()}};
        QJsonArray owners;for(const auto &[id,name]:host.scene_snapshot().object_names)owners.append(QJsonObject{{"runtime_id",QString::number(id)},{"name",QString::fromStdString(name)}});value["owners"]=owners;
        if(document&&selected_material>=0){
          const auto &set=document->set(snapshot.materials[selected_material].session_id);
          const auto found=std::find_if(set.layers.begin(),set.layers.end(),[&](const auto &layer){return layer.id==set.selected_layer;});
          if(found==set.layers.end())throw std::runtime_error("BJ actual selected Fill missing");
          value["runtime_uid"]=QString::number(set.material_session_uid);value["set"]=set.id.toString();value["layer"]=found->id.toString();
          value["authored_layer"]=bts::projection_ui_proof::layer_json(*document,set.id,found->id);value["layer_count"]=qint64(set.layers.size());
          {bts::EditStateScope metadata;value["edit_state"]=document->edit_state();}
          QJsonArray tiles;for(int tile:set.tiles)tiles.append(tile);value["tiles"]=tiles;
          QJsonArray dimensions,precision;
          for(int tile:set.tiles)dimensions.append(QJsonArray{tile,set.width(tile),set.rows(tile)});
          for(int channel:{0,2,4})precision.append(QJsonArray{channel,int(set.precision[channel])});
          value["dimensions"]=dimensions;value["precision"]=precision;
          if(workspace->property("bjReadGpu").toBool()&&!host.texture_updates_pending()){
            const std::array<std::array<int,2>,5> xy{{{32,64},{96,64},{64,64},{4,4},{124,124}}};QJsonArray packets;
            for(int tile:set.tiles)for(int channel:{0,2,4})if(set.channels[channel]){
              const auto pixels=host.diagnostic_gpu_pixels(set.material_session_uid,channel,xy,tile);
              if(pixels.size()!=xy.size())throw std::runtime_error("BJ GPU point packet unavailable");
              QJsonArray points;for(std::size_t i=0;i<xy.size();++i){QJsonArray rgba;for(float v:pixels[i])rgba.append(v);points.append(QJsonObject{{"xy",QJsonArray{xy[i][0],xy[i][1]}},{"rgba",rgba}});}
              packets.append(QJsonObject{{"set",set.id.toString()},{"tile",tile},{"channel",channel},{"points",points}});
            }
            value["gpu"]=packets;
          }
          if(found->kind==bts::LayerKind::Fill){
            auto native=found->fill.native_material;
            value["native"]=bool(native);value["height_policy"]=native?int(native->height_policy):0;value["height_enabled"]=found->channels[4];value["height_kind"]=native?(native->height_policy==bts::NativeHeightPolicy::None?"none":"raw-authored-float"):(found->channels[4]?"ordinary-bitmap-bump":"none");
          }
        }
        value["gizmo_drag"]=viewport->diagnostic_fill_gizmo_drag();
        workspace->setProperty("bjState",value);
      });
      add("guiBjSave",[&]{
        if(!document||selected_material<0)throw std::runtime_error("BJ Save without project");
        const auto &set=document->set(snapshot.materials[selected_material].session_id);
        const auto path=workspace->property("bjProject").toString();if(path.isEmpty())throw std::runtime_error("BJ private Save path missing");
        const auto expected=bts::projection_ui_proof::witness(*document,assets.project_assets(),set.id,set.selected_layer);
        QFile file(path+".bj-witness.json");if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("BJ witness write failed");file.write(QJsonDocument(expected).toJson());file.close();
        save_document(path);
      });
      add("guiBjOpen",[&]{const auto path=workspace->property("bjProject").toString();if(path.isEmpty())throw std::runtime_error("BJ private Open path missing");begin_open_document(path);});
      add("guiBjValidateOpen",[&]{
        if(!document)throw std::runtime_error("BJ validation before actual Open admission");
        QFile file(workspace->property("bjProject").toString()+".bj-witness.json");if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("BJ expected packed witness absent");
        const auto expected=QJsonDocument::fromJson(file.readAll()).object();
        workspace->setProperty("bjOpenProof",bts::projection_ui_proof::validate(*document,assets.project_assets(),expected));
        // A real UI row gesture in the actor selects its persistent UUID. This
        // action does not repair selection, resources, transforms or channels.
      });
    }
