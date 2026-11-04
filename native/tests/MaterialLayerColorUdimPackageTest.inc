// Called with real decoded originals and an untouched compatible target. No GL.
QJsonObject verify_linear_color_udim_package(const MaterialLayerParameters &value,const TextureSet &target,const MaterialParameterDecoder &decoder,const MaterialParameterProcessor &processor,const MaterialParameterTargetValidator &native_gate){
 const auto json=material_layer_parameters_json(value);assert(json["version"]==2&&json["source_members_version"]==4);
 const auto metadata=read_material_layer_parameters_json(json);assert(material_layer_parameters_json(metadata)==json);
 QTemporaryDir directory;assert(directory.isValid());const auto path=directory.filePath("linear-color-udim-package");std::atomic_bool cancel=false;write_material_layer_parameter_package(path,value,cancel);auto parsed=read_material_layer_parameter_package(path,cancel);assert(material_layer_parameters_json(parsed)==json);
 // Real product worker: native owner admission, actual package reader/shared
 // decoder and per-member native validator. These hooks inspect a prepared
 // candidate only; no CPU test claims GPU/catalog publication.
 QJsonObject product_import;
 {
  SceneSnapshot scene;MaterialSummary material;material.session_id=target.material_session_uid;material.name=target.name;material.uniform_editable=true;scene.materials.push_back(material);ImportSummary imported;imported.materials.push_back({material.session_id,material.name});imported.materials.back().tiles=target.tiles;
  NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;options.uv_workflow=UvWorkflow::PreserveTiles;TextureDocument document(options,imported,scene);const auto before=document.edit_state();
  MaterialParameterImportTarget identity;identity.project=document.id();identity.set=document.set(material.session_id).id;identity.uid=material.session_id;identity.revision=1;identity.working_space=value.working_space;identity.scene_linear_to_xyz=value.scene_linear_to_xyz;identity.signature=before;
  bool current=true,busy=true;int gates=0,ready=0,claims=0,releases=0,publications=0;MaterialParameterImportHooks hooks;
  hooks.current=[&](const auto &candidate){return current&&candidate.project==identity.project&&candidate.set==identity.set&&candidate.uid==identity.uid&&candidate.revision==1&&candidate.signature==before;};
  hooks.validate=[&](const auto &candidate){assert(candidate.uid==identity.uid);native_gate(document.set(candidate.uid));++gates;};hooks.busy=[&]{++ready;return busy;};hooks.claim=[&](const auto &token){assert(!token.isNull());++claims;return true;};hooks.release=[&](const auto &token){assert(!token.isNull());++releases;};
  hooks.publish=[&](auto &candidate,const auto &token){assert(!token.isNull()&&candidate.document&&candidate.originals.size()==value.originals.size());const auto &set=candidate.document->set(identity.uid);assert(set.material_layering.enabled);for(int input=0;input<3;++input){assert(layer_material_json(set.material_layering.materials[input])==layer_material_json(value.materials[input]));const auto &source=*set.material_layering.materials[input].textures[0];assert(source.tiles&&source.tiles->size()==2);for(const auto &[tile,member]:*source.tiles){assert(member->pixels&&QString::fromStdString(member->input_space)==value.color_members[input].at(tile).input_space&&QString::fromStdString(material_layer_source_color_transform(*member).processor)==value.color_members[input].at(tile).processor);}}++publications;return true;};
  MaterialLayerParameterImportJob job(std::move(hooks));const auto until=[&](auto condition){QElapsedTimer deadline;deadline.start();while(!condition()&&deadline.elapsed()<30000){QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents,5);QThread::msleep(1);}assert(condition());};
  job.start(identity,document,path);job.cancel();until([&]{return !job.pending();});assert(job.status().cancelled&&job.status().error.isEmpty()&&claims==0&&publications==0&&document.edit_state()==before);
  job.start(identity,document,path);until([&]{return ready>0||!job.pending();});assert(job.pending()&&ready>0&&job.status().error.isEmpty());current=false;until([&]{return !job.pending();});assert(job.status().cancelled&&claims==0&&publications==0&&document.edit_state()==before);current=true;
  busy=false;job.start(identity,document,path);until([&]{return !job.pending();});assert(job.status().error.isEmpty()&&!job.status().cancelled&&claims==1&&releases==1&&publications==1&&gates>0&&document.edit_state()==before);
  for(bool space_lie:{false,true}){auto lying=value;auto &member=lying.color_members[0].at(1002);if(space_lie)member.input_space="sRGB";else member.processor="not-the-native-processor";const auto bad=directory.filePath(space_lie?"lying-member-space":"lying-member-processor");write_material_layer_parameter_package(bad,lying,cancel);job.start(identity,document,bad);until([&]{return !job.pending();});assert(job.status().error.contains("member processor/input space")&&claims==1&&releases==1&&publications==1&&document.edit_state()==before);}
  job.shutdown();product_import={{"actual_reader_decoder_and_native_member_validation",true},{"cancel_pending_worker",true},{"stale_ready_result_retired",true},{"lying_member_space_processor_rejections",2},{"one_owner_lease_released",true},{"live_document_unchanged",true},{"GPU_publication_tested_here",false}};
 }
 int calls=0,admissions=0,rejected=0;const auto decode=[&](const FillTexture &source,const MaterialParameterOriginal &original,bool mask){++calls;return decoder(source,original,mask);};
 const auto linear=[&](const FillTexture &source){++admissions;const auto transforms=material_layer_color_source_transforms(source);assert(source.tiles&&transforms.size()==source.tiles->size());};
 const auto rebuild=[&](const MaterialLayerParameters &parameters,const MaterialParameterColorSequenceValidator &gate){return reconstruct_material_layer_parameters(target,parameters,value.working_space,value.scene_linear_to_xyz,decode,processor,native_gate,gate);};
 const auto before=layer_material_json(value.materials[0]);
 const auto reject=[&](const auto &operation){bool failed=false;try{operation();}catch(const std::invalid_argument &){failed=true;}assert(failed&&layer_material_json(value.materials[0])==before&&!target.material_layering.allocated());++rejected;};
 // Capability absence is a pre-decoder error, independently of the manifest's
 // claimed processor IDs. Native target admission is also still pre-decoder.
 reject([&]{rebuild(parsed,{});});assert(calls==0&&admissions==0);
 reject([&]{reconstruct_material_layer_parameters(target,parsed,value.working_space,value.scene_linear_to_xyz,decode,processor,{},linear);});assert(calls==0);
 const auto rebuilt=rebuild(parsed,linear);assert(admissions==3&&rebuilt.material_layering.enabled);
 for(int input=0;input<3;++input)assert(layer_material_json(rebuilt.material_layering.materials[input])==layer_material_json(value.materials[input]));
 for(bool lie_about_space:{false,true}){auto lying=parsed;auto &member=lying.color_members[0].at(1002);if(lie_about_space)member.input_space="sRGB";else member.processor="not-the-native-processor";const int old=calls;reject([&]{rebuild(lying,linear);});assert(calls>old);}
 // The real decoder honors this override, but the actual native linear gate
 // rejects it. An apparently self-consistent metadata table grants no bypass.
 {auto nonlinear=parsed;auto source=std::make_shared<FillTexture>(*nonlinear.materials[0].textures[0]);source->source_space="sRGB";auto members=std::make_shared<std::map<int,std::shared_ptr<const FillTexture>>>();for(const auto &[tile,old]:*source->tiles){auto member=std::make_shared<FillTexture>(*old);member->source_space="sRGB";members->emplace(tile,member);}source->tiles=members;nonlinear.materials[0].textures[0]=source;const int old=calls;reject([&]{rebuild(nonlinear,linear);});assert(calls>old);}
 {auto missing=json;auto materials=missing["materials"].toArray();auto row=materials[0].toObject();auto table=row["color_processor"].toObject();auto members=table["members"].toArray();members.removeLast();table["members"]=members;row["color_processor"]=table;materials[0]=row;missing["materials"]=materials;reject([&]{read_material_layer_parameters_json(missing);});}
 {auto legacy=json;legacy["version"]=1;legacy["source_members_version"]=3;reject([&]{read_material_layer_parameters_json(legacy);});}
 // Nonsequence emission still uses the original three-field input row and
 // original v1/3 version pair; its processor remains the actual single source.
 auto ordinary=value;for(int input=0;input<3;++input){auto single=ordinary.materials[input].textures[0]->tiles->at(1001);auto descriptor=std::make_shared<FillTexture>(*single);const auto original=std::find_if(ordinary.originals.begin(),ordinary.originals.end(),[&](const auto &entry){return entry.asset.toString(QUuid::WithoutBraces).toStdString()==single->asset;});assert(original!=ordinary.originals.end());
  // Keep other sequence/raw uses intact by assigning this ordinary view a new
  // resource UUID with exactly the same original bytes and one tile=0 row.
  MaterialParameterOriginal file;file.asset=QUuid::createUuid();file.files.push_back(original->files.front());file.files.front().descriptor.tile=0;descriptor->asset=file.asset.toString(QUuid::WithoutBraces).toStdString();descriptor->hash=texture_sources_hash({file.files.front().descriptor}).toStdString();ordinary.originals.push_back(std::move(file));ordinary.materials[input].textures[0]=descriptor;ordinary.color_members[input].clear();ordinary.color_processors[input]=processor(*descriptor);
 }
 const auto legacy=material_layer_parameters_json(ordinary);assert(legacy["version"]==1&&legacy["source_members_version"]==3);for(const auto &row:legacy["materials"].toArray())assert(row.toObject().size()==3&&row.toObject()["color_processor"].isString());assert(material_layer_parameters_json(read_material_layer_parameters_json(legacy))==legacy);
 return {{"version2_exact_member_table",true},{"native_linear_members",admissions},{"missing_callback_predecode",true},{"product_worker_v2",product_import},{"negative_cases",rejected},{"ordinary_version1_roundtrip",true}};
}
