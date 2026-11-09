// SPDX-License-Identifier: GPL-3.0-or-later
// Product Import uses its real button, folder chooser and OwnerWorkerJob. The
// older diagnostic publication profile remains an independent control.
 bool import_ui_=qEnvironmentVariableIsSet("BTS_PD149_PARAMETER_IMPORT");
 QTimer import_watch_;bool import_cancelled_preparing_=false;std::shared_ptr<const FillTexture> import_legacy_source_;
 void choose_import(bool cancel_dialog=false){
  auto &button=get<QAbstractButton>("importMaterialLayerParameters");visible(button);
  QTimer::singleShot(0,window_,[this,cancel_dialog]{auto *dialog=window_->findChild<QFileDialog *>("materialParameterImportDialog");if(!dialog||dialog->fileMode()!=QFileDialog::Directory){fail("Import must use the actual package-folder dialog");return;}if(cancel_dialog){dialog->reject();return;}dialog->setDirectory(parameter_path());dialog->selectFile(parameter_path());QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);});
  button.click();
 }
 void require_import_unchanged(){require(hooks_.document()->to_json()==reconstruction_before_&&hooks_.document()->shader_instances().to_json()==reconstruction_old_library_&&catalog()==catalog_&&hooks_.state()["undo_count"].toInt()==reconstruction_history_,"Cancelled/stale Import changed document, library, catalog or history");}
 void begin_import_test(){
  require(!QFileInfo(source_).exists()&&set().users[0].present&&!set().material_layering.allocated(),"Import needs fresh New, unrelated slot0 and hidden source");
  reconstruction_before_=hooks_.document()->to_json();reconstruction_others_=other_sets();reconstruction_old_library_=hooks_.document()->shader_instances().to_json();reconstruction_baseline_=ordinary_colors();reconstruction_history_=hooks_.state()["undo_count"].toInt();catalog_=catalog();
  // This diagnostic reads package metadata/originals for independent checks;
  // production Import still performs its own guarded reader and shared decoder.
  std::atomic_bool cancel=false;reconstructed_.parameters=read_material_layer_parameter_package(parameter_path(),cancel);
  choose_import(true);require_import_unchanged();require(!hooks_.state()["parameter_import_pending"].toBool(),"Folder Cancel started Import work");reconstruction_proof_["folder_cancel_unchanged"]=true;stage_=400;
 }
 void advance_import(){
  const auto state=hooks_.state();
  if((stage_==402||stage_==403||stage_==405||stage_==407)&&!state["parameter_import_pending"].toBool()&&window_->findChild<QProgressDialog *>("materialParameterImportProgress"))return;
  switch(stage_){
   case 400:{bNode *principled=nullptr;for(bNode &node:material().nodetree->nodes)if(STREQ(node.idname,"ShaderNodeBsdfPrincipled")){require(!principled,"Import fixture has multiple Principled nodes");principled=&node;}require(principled,"Import fixture canonical Principled missing");auto &alpha=input(*principled,"Alpha");auto *value=static_cast<bNodeSocketValueFloat *>(alpha.default_value);const auto saved=value->value;const auto restore=qScopeGuard([&]{value->value=saved;});value->value=.25f;choose_import();require(!hooks_.state()["parameter_import_pending"].toBool()&&get<QLabel>("materialLayeringScope").text().contains("Alpha"),"Actual native Alpha must reject Import before worker decode");require_import_unchanged();reconstruction_proof_["actual_native_surface_rejected_before_worker"]=true;stage_=401;break;}
   case 401:choose_import();require(hooks_.state()["parameter_import_pending"].toBool(),"Real Import did not dispatch its worker");QMetaObject::invokeMethod(&get<QProgressDialog>("materialParameterImportProgress"),"canceled",Qt::DirectConnection);stage_=402;break;
   case 402:if(state["parameter_import_pending"].toBool())return;require(state["parameter_import_error"].toString().isEmpty(),"Normal Import cancellation delivered an error");require_import_unchanged();reconstruction_proof_["worker_cancel_retired_unchanged"]=true;{auto &edit=get<QAbstractButton>("addPaintLayer");visible(edit);choose_import();edit.click();}stage_=403;break;
   case 403:if(state["parameter_import_pending"].toBool())return;require(!set().material_layering.allocated()&&catalog()==catalog_&&state["undo_count"].toInt()==reconstruction_history_+1,"Stale Import published after an ordinary edit");action("undoDocument");stage_=404;break;
   case 404:require_import_unchanged();reconstruction_proof_["stale_result_rejected"]=true;
    QObject::connect(&import_watch_,&QTimer::timeout,window_,[this]{const auto value=hooks_.state();if(value["parameter_import_pending"].toBool()&&value["parameter_import_text"]=="Preparing material images…"){auto *progress=window_->findChild<QProgressDialog *>("materialParameterImportProgress");if(progress){import_cancelled_preparing_=true;import_watch_.stop();QMetaObject::invokeMethod(progress,"canceled",Qt::DirectConnection);}}});import_watch_.start(1);choose_import();stage_=405;break;
   case 405:if(state["parameter_import_pending"].toBool())return;import_watch_.stop();require(import_cancelled_preparing_&&state["parameter_import_error"].toString().isEmpty(),"Import did not reach and cancel real scoped GPU preparation");require_import_unchanged();reconstruction_proof_["preparing_cancel_releases_owner_unchanged"]=true;
    {const auto &source=*reconstructed_.parameters.masks[0];const auto found=std::find_if(reconstructed_.parameters.originals.begin(),reconstructed_.parameters.originals.end(),[&](const auto &item){return item.asset.toString(QUuid::WithoutBraces).toStdString()==source.asset;});require(found!=reconstructed_.parameters.originals.end(),"Legacy completion fixture original missing");StudioAsset asset;asset.id=found->asset;asset.name=QString::fromStdString(source.name);asset.filename=found->files.front().descriptor.filename;asset.path=found->files.front().descriptor.path;asset.scope=AssetScope::Project;asset.usages={"texture"};for(const auto &file:found->files)asset.texture_files.push_back(file.descriptor);import_legacy_source_=decode_fill_texture(asset,true,source.source_space);}stage_=406;break;
   case 406:if(!hooks_.host.prepare_texture_resource(import_legacy_source_)){viewport_->update();return;}
    // Deliberately NO old cancel: successful legacy callers retain their vector.
    reconstruction_proof_["completed_legacy_without_cancel"]=true;choose_import();stage_=407;break;
   case 407:{if(state["parameter_import_pending"].toBool())return;require(state["parameter_import_error"].toString().isEmpty()&&set().material_layering.allocated(),"Actual Import did not publish after completed legacy preparation");require(state["undo_count"].toInt()==reconstruction_history_+1,"Import was not one full-document Undo");
    const auto rows=state["project_resources"].toArray();for(const auto &original:reconstructed_.parameters.originals){int count=0;for(const auto &row:rows){const auto entry=row.toObject();if(QUuid(entry["id"].toString())==original.asset){require(entry["hash"].toString()==texture_sources_hash([&]{std::vector<TextureSourceFile> files;for(const auto &member:original.files)files.push_back(member.descriptor);return files;}()),"Imported original hash changed");++count;}}require(count==1,"Imported original catalog row was not published exactly once");}
    for(int i=0;i<4;++i)require(layer_material_json(set().material_layering.materials[i])==layer_material_json(reconstructed_.parameters.materials[i]),"Import changed source/SRT metadata");
    reconstruction_after_=hooks_.document()->to_json();reconstruction_new_library_=hooks_.document()->shader_instances().to_json();require(reconstruction_new_library_!=reconstruction_old_library_,"Import did not create a fresh local shader library assignment");
    reconstruction_proof_["actual_import_button_and_directory"]=true;reconstruction_proof_["actual_full_document_publication"]=true;reconstruction_proof_["one_history_commit"]=true;reconstruction_proof_["catalog_originals_once"]=true;reconstruction_proof_["catalog_original_rows"]=int(reconstructed_.parameters.originals.size());stage_=303;break;}
   default:throw std::logic_error("Unknown product Import stage");
  }
  frame_=hooks_.state()["render_frames"].toInt()+2;viewport_->update();
 }
 void retire_import()noexcept{import_watch_.stop();QObject::disconnect(&import_watch_,nullptr,window_,nullptr);import_legacy_source_.reset();}
