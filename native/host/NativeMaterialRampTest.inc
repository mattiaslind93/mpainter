// Linked ColorRamp overrides affect the runtime copy, never the source archive.
{
 std::unique_ptr<Main,decltype(&BKE_main_free)> owner(BKE_main_new(),BKE_main_free);
 auto *material=BKE_material_add(owner.get(),"Native ramp source");auto &tree=*material->nodetree;
 auto *principled=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled"));
 auto *out=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));
 auto *ramp=bke::node_add_node(nullptr,tree,UString("ShaderNodeValToRGB"));
 auto &original=*static_cast<ColorBand *>(ramp->storage);original.data[0].r=.9f;
 bke::node_add_link(tree,*ramp,socket(*ramp,"Color",false),*principled,socket(*principled,"Base Color",true));
 bke::node_add_link(tree,*principled,socket(*principled,"BSDF",false),*out,socket(*out,"Surface",true));
 BKE_ntree_update_after_single_tree_change(*owner,tree);
 NativeMaterialDescription description;description.material="Native ramp source";description.surface_type="ShaderNodeBsdfPrincipled";
 NativeMaterialParameter parameter;parameter.identifier="Base Color";parameter.name="Color";parameter.type=NativeParameterType::Color;parameter.source_node="ShaderNodeValToRGB";parameter.source_socket="Color";
 parameter.ramp={{"color_mode",0},{"interpolation",0},{"hue_interpolation",0},{"stops",QJsonArray{QJsonObject{{"position",0},{"color",QJsonArray{-.25,.5,1.5,1}}},QJsonObject{{"position",1},{"color",QJsonArray{-.25,.5,1.5,1}}}}}};
 description.parameters.push_back(parameter);std::atomic_bool cancel=false;auto graph=std::make_shared<NativeMaterialGraph>(std::move(owner),material,description,cancel);
 bNodeTree *color=nullptr;for(const auto &channel:graph->channels)if(channel.channel==0)color=channel.tree;
 require(color,"Native ramp channel was not prepared");NativeMaterialGpuProgram program(*color,graph->parameters.size(),0);NativeMaterialGpuContext context;
 program.dispatch(target.get(),graph->parameters,context,{0,0,width,height});GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);
 auto *pixels=static_cast<float *>(GPU_texture_read(target.get(),GPU_DATA_FLOAT,0));require(pixels,"Native ramp readback missing");const auto release=qScopeGuard([&]{MEM_delete(pixels);});++reads;
 for(int p=0;p<width*height;++p){const std::array<float,4> expected{-.25f,.5f,1.5f,1};for(int c=0;c<4;++c)require(std::abs(pixels[p*4+c]-expected[c])<2e-6,"Native linked ramp override was ignored or clamped");}
 require(original.data[0].r==.9f&&graph->original_inputs_preserved,"Native ramp changed its encoded source");
 NativeMaterialGpuLibrary library;library.install("ramp-source","ramp-source-content",graph);library.render("ramp-source",0,description,target.get(),context,{0,0,width,height});const auto compilations=GPU_pass_global_compilation_count();
 auto variant=description;auto changed=variant.parameters[0].ramp["stops"].toArray();for(int i=0;i<changed.size();++i){auto stop=changed[i].toObject();stop["color"]=QJsonArray{.5,.25,2,1};changed[i]=stop;}auto middle=changed[0].toObject();middle["position"]=.5;changed.insert(1,middle);variant.parameters[0].ramp["stops"]=changed;
 require(library.signature("ramp-source",0,description)!=library.signature("ramp-source",0,variant)&&library.dependencies("ramp-source",0).parameters==std::vector<std::size_t>{0},"Ramp change did not invalidate its dependent channel");
 for(int pass=0;pass<2;++pass){QElapsedTimer ramp_timer;ramp_timer.start();library.render("ramp-source",0,pass?description:variant,target.get(),context,{0,0,width,height});ramp_submit_ms=std::max(ramp_submit_ms,ramp_timer.nsecsElapsed()/1e6);GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);auto *pixels=static_cast<float *>(GPU_texture_read(target.get(),GPU_DATA_FLOAT,0));require(pixels,"Dynamic ramp readback missing");const auto release=qScopeGuard([&]{MEM_delete(pixels);});++reads;const std::array<float,4> expected=pass?std::array<float,4>{-.25f,.5f,1.5f,1}:std::array<float,4>{.5f,.25f,2,1};for(int p=0;p<width*height;++p)for(int c=0;c<4;++c)require(std::abs(pixels[p*4+c]-expected[c])<2e-6,"Shared native program retained another instance's ColorRamp");}
 // The panel can duplicate coincident stops up to native ColorBand capacity,
 // or remove stops down to one. Exercise both boundaries, then restore the
 // other instance on the same program without a source or shader replacement.
 for(int count:{1,32}){
  auto boundary=description;QJsonArray stops;
  for(int n=0;n<count;++n)stops.append(QJsonObject{{"position",n+1==count?1:0},{"color",QJsonArray{-1.25,.25,2,1}}});
  boundary.parameters[0].ramp["stops"]=stops;
  for(int pass=0;pass<2;++pass){
   library.render("ramp-source",0,pass?description:boundary,target.get(),context,{0,0,width,height});GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);
   auto *pixels=static_cast<float *>(GPU_texture_read(target.get(),GPU_DATA_FLOAT,0));require(pixels,"Native topology readback missing");const auto release=qScopeGuard([&]{MEM_delete(pixels);});++reads;
   const std::array<float,4> expected=pass?std::array<float,4>{-.25f,.5f,1.5f,1}:std::array<float,4>{-1.25f,.25f,2,1};
   for(int p=0;p<width*height;++p)for(int c=0;c<4;++c)require(std::isfinite(pixels[p*4+c])&&std::abs(pixels[p*4+c]-expected[c])<2e-6,"Native stop topology failed or contaminated another instance");
  }
 }
 require(GPU_pass_global_compilation_count()==compilations&&library.statistics().sources==1&&library.statistics().programs==1,"Live ramp edits recompiled or duplicated source programs");
 auto bad=parameter.ramp;auto stops=bad["stops"].toArray();auto stop=stops.first().toObject();stop["position"]=-.1;stops[0]=stop;bad["stops"]=stops;bool rejected=false;try{native_material_ramp(bad);}catch(const std::invalid_argument &){rejected=true;}require(rejected,"Invalid native ramp position was accepted");
}
