// Draft CPU cases in the existing native dispatcher. No GL/pixel compositor.
int verify_srgb_processor_structure(){
 namespace OCIO=OCIO_NAMESPACE;int rejected=0;
 const auto make=[](){auto group=OCIO::GroupTransform::Create();auto curve=OCIO::ExponentWithLinearTransform::Create();const double g[]{2.4,2.4,2.4,1},o[]{.055,.055,.055,0};curve->setGamma(g);curve->setOffset(o);curve->setNegativeStyle(OCIO::NEGATIVE_LINEAR);group->appendTransform(curve);group->appendTransform(OCIO::MatrixTransform::Create());return group;};
 const auto reject=[&](const OCIO::ConstGroupTransformRcPtr &group){bool failed=false;try{material_layer_processor_transform(group,"structural fixture",true);}catch(const std::invalid_argument &){failed=true;}assert(failed);++rejected;};
 const auto group=make();const auto transform=material_layer_processor_transform(group,"structural fixture",true);
 assert(transform.srgb&&transform.gamma==2.4&&transform.offset==.055&&transform.breakpoint==.055/1.4&&std::isfinite(transform.slope));
 // Compare this narrow algebra with OCIO itself, including negative/toe/HDR
 // values. This does not grant float-image authoring eligibility.
 auto config=OCIO::Config::CreateRaw();// Algebra checks exclude OCIO's optional fast pow approximation; the real
 // decoder and artist reference keep their default CPU optimization unchanged.
 auto cpu=config->getProcessor(group)->getOptimizedCPUProcessor(OCIO::OPTIMIZATION_LOSSLESS);
 for(float x:{-100.f,-.055f,-.01f,0.f,.01f,float(transform.breakpoint),.5f,1.f,4.f}){float p[]{x,x,x,.25f};cpu->applyRGBA(p);for(int c=0;c<3;++c)assert(std::isfinite(p[c])&&std::abs(double(p[c])-material_layer_moncurve(x,transform))<3e-5);assert(p[3]==.25f);}
 {auto candidate=make();candidate->setDirection(OCIO::TRANSFORM_DIR_INVERSE);reject(candidate);}
 {auto candidate=make();auto curve=std::dynamic_pointer_cast<OCIO::ExponentWithLinearTransform>(candidate->getTransform(0));curve->setDirection(OCIO::TRANSFORM_DIR_INVERSE);reject(candidate);}
 {auto candidate=make();auto curve=std::dynamic_pointer_cast<OCIO::ExponentWithLinearTransform>(candidate->getTransform(0));curve->setNegativeStyle(OCIO::NEGATIVE_MIRROR);reject(candidate);}
 {auto candidate=make();auto curve=std::dynamic_pointer_cast<OCIO::ExponentWithLinearTransform>(candidate->getTransform(0));const double gamma[]{2.4,2.4,2.4,2};curve->setGamma(gamma);reject(candidate);}
 {auto candidate=make();auto matrix=std::dynamic_pointer_cast<OCIO::MatrixTransform>(candidate->getTransform(1));double m[16];matrix->getMatrix(m);m[3]=.125;matrix->setMatrix(m);reject(candidate);}
 {auto candidate=make();auto matrix=std::dynamic_pointer_cast<OCIO::MatrixTransform>(candidate->getTransform(1));const double offset[]{.01,0,0,0};matrix->setOffset(offset);reject(candidate);}
 {auto candidate=OCIO::GroupTransform::Create();candidate->appendTransform(OCIO::MatrixTransform::Create());candidate->appendTransform(make()->getTransform(0));reject(candidate);}
 {auto candidate=make();candidate->appendTransform(OCIO::RangeTransform::Create());reject(candidate);}
 return rejected;
}
int verify_srgb_original_carrier(){
 using namespace blender;using Buffer=std::unique_ptr<ImBuf,decltype(&IMB_freeImBuf)>;
 std::unique_ptr<Main,decltype(&BKE_main_free)> live(BKE_main_new(),BKE_main_free);IMB_colormanagement_working_space_init_default(live.get());auto *scene=BKE_scene_add(live.get(),"sRGB same-encoding data and Color");for(auto &layer:scene->view_layers)BKE_view_layer_synced_ensure(*live,scene,&layer);
 StudioAsset original;original.id=QUuid::createUuid();original.name="Byte sRGB fixture";original.filename="srgb-original.tga";original.scope=AssetScope::Project;original.usages={"texture"};
 // Uncompressed bottom-origin 2x2 RGBA TGA. RGB at alpha zero is intentional.
 original.bytes=QByteArray(18,0);original.bytes[2]=2;original.bytes[12]=2;original.bytes[14]=2;original.bytes[16]=32;original.bytes[17]=8;
 const std::array<std::array<unsigned char,4>,4> rgba{{{0,0,0,0},{255,255,255,255},{64,128,192,0},{192,32,96,128}}};for(const auto &p:rgba)for(int c:{2,1,0,3})original.bytes.append(char(p[c]));
 const auto raw=decode_fill_texture(original,true),color=decode_fill_texture(original,false,"sRGB");assert(!raw->floating_point&&!color->floating_point&&raw->hash==color->hash&&raw->pixels!=color->pixels);
 const auto transform=material_layer_source_color_transform(*color);assert(transform.srgb&&!transform.processor.empty());
 for(int c=0;c<3;++c)assert((*raw->pixels)[2][c]==float(rgba[2][c])/255);assert((*raw->pixels)[2][3]==0&&(*color->pixels)[2][3]==0&&(*color->pixels)[2][1]!=0);
 // EOTF happens per texel: half black/white is .5, not EOTF(.5).
 assert(std::abs(.5*(material_layer_moncurve(0,transform)+material_layer_moncurve(1,transform))-.5)<1e-12);assert(std::abs(material_layer_moncurve(.5,transform)-.5)>.2);
 auto floating=*color;floating.floating_point=true;bool rejected=false;try{material_layer_source_color_transform(floating);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
 auto *material=BKE_material_add(live.get(),"sRGB owner");auto &tree=*material->nodetree;auto *surface=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled")),*output=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));
 const auto socket=[](bNode &n,const char *name,bool input)->bNodeSocket &{for(auto &s:input?n.inputs:n.outputs)if(STREQ(s.identifier,name))return s;throw std::runtime_error("sRGB fixture socket missing");};
 BKE_ntree_update_after_single_tree_change(*live,tree);bke::node_add_link(tree,*surface,socket(*surface,"BSDF",false),*output,socket(*output,"Surface",true));BKE_ntree_update_after_single_tree_change(*live,tree);
 SceneSnapshot description;MaterialSummary summary;summary.session_id=material->id.session_uid;summary.name=material->id.name+2;summary.uniform_editable=true;description.materials.push_back(summary);ImportSummary imported;imported.materials.push_back({summary.session_id,summary.name});TextureDocument document({},imported,description);auto &set=document.set(summary.session_id);configure_material_layering(set,true);set.material_layering.materials[0].textures[0]=color;set.material_layering.materials[1].textures[2]=raw;
 for(int slot:set.material_layering.mask_slots){auto *image=reinterpret_cast<Image *>(BKE_id_new(live.get(),ID_IM,"sRGB opacity"));describe_source(*image,1,1);image->runtime->gpuflag&=~IMA_HOSTED_IMMUTABLE_SOURCE;auto *n=bke::node_add_node(nullptr,tree,UString("ShaderNodeTexImage"));STRNCPY(n->name,("__BTS_UserPixels"+std::to_string(slot)).c_str());n->id=&image->id;id_us_plus(n->id);}
 MaterialLayerImages images{};std::array<Image *,2> aliases{};
 for(int i=0;i<2;++i){const auto &source=i?raw:color;auto *image=reinterpret_cast<Image *>(BKE_id_new(live.get(),ID_IM,"sRGB typed alias"));id_us_min(&image->id);describe_source(*image,2,2);Buffer header(material_layer_source_pixel_view(*source),IMB_freeImBuf);assert(BKE_image_assign_hosted_immutable_source(image,1001,header.get()));aliases[i]=image;images[i][i?2:0]=image;}
 MaterialLayerShader shader;shader.apply(*live,*material,set.material_layering,1,1,false,images);
 const auto capture=[&]{return capture_material_layer_source_bindings(*live,document);};assert(capture().size()==2);int rejected_graphs=0;
 const auto reject=[&]{bool failed=false;try{capture();}catch(const std::invalid_argument &){failed=true;}assert(failed);++rejected_graphs;};
 auto *tap=bke::node_find_node_by_name(tree,"__BTS_LayerMaterial0ColorTap2");assert(tap&&tap->id==&aliases[0]->id);auto &settings=*static_cast<NodeTexImage *>(tap->storage);
 settings.interpolation=SHD_INTERP_LINEAR;reject();settings.interpolation=SHD_INTERP_CLOSEST;
 settings.iuser.tile=1002;reject();settings.iuser.tile=1001;
 tap->id=&aliases[1]->id;reject();tap->id=&aliases[0]->id;
 {auto *node=bke::node_find_node_by_name(tree,"__BTS_LayerMaterial0SrgbFraction");const auto op=node->custom1;node->custom1=NODE_VECTOR_MATH_FLOOR;reject();node->custom1=op;}
 {auto *node=bke::node_add_node(nullptr,tree,UString("ShaderNodeValue"));STRNCPY(node->name,"__BTS_LayerMaterial0SrgbUnknown");reject();bke::node_remove_node(live.get(),tree,*node,true);}
 {auto *marker=bke::node_find_node_by_name(tree,"__BTS_LayerMaterial0SrgbRepresentation");auto &v=static_cast<bNodeSocketValueFloat *>(static_cast<bNodeSocket *>(marker->outputs.first)->default_value)->value;v=1;reject();v=0;}
 const auto bindings=capture();const auto before=document.to_json();const int live_users=aliases[0]->id.us;
 bke::blendfile::PartialWriteContext snapshot(*live);snapshot.bmain.colorspace=live->colorspace;using Context=bke::blendfile::PartialWriteContext;auto *copy_scene=reinterpret_cast<Scene *>(snapshot.id_add(&scene->id,{Context::ADD_DEPENDENCIES}));auto *copy_material=reinterpret_cast<Material *>(snapshot.id_add(&material->id,{Context::ADD_DEPENDENCIES}));auto owners=retain_material_layer_source_snapshot_headers(*live,snapshot.bmain);
 ProjectResourceImages resources;pack_project_assets(snapshot.bmain,*copy_scene,{original},&resources,&snapshot);pack_material_layer_source_bindings(snapshot,*copy_scene,bindings,resources);assert(snapshot.is_valid()&&resources.by_key.size()==1&&document.to_json()==before&&aliases[0]->id.us==live_users);
 auto *carrier=resources.by_key.begin()->second;const auto *part=static_cast<const ImagePackedFile *>(carrier->packedfiles.first);assert(part&&part->packedfile&&part->packedfile->size==original.bytes.size()&&!std::memcmp(part->packedfile->data,original.bytes.data(),original.bytes.size()));
 int uses=0;for(const auto &node:copy_material->nodetree->nodes)if(node.id==&carrier->id)++uses;assert(uses==5);for(const auto *alias:aliases)for(const Image &image:snapshot.bmain.images)assert(image.id.session_uid!=alias->id.session_uid);
 MaterialLayerShader::color_source_representation(snapshot.bmain,*copy_material,0,*color,true,true);
 // Deliberately mutate a STOCK per-texel curve, not just its metadata row.
 auto *gamma=bke::node_find_node_by_name(*copy_material->nodetree,"__BTS_LayerMaterial0SrgbDecode0XGamma");assert(gamma);auto &value=static_cast<bNodeSocketValueFloat *>(static_cast<bNodeSocket *>(gamma->outputs.first)->default_value)->value;const float old=value;value=2;rejected=false;try{MaterialLayerShader::color_source_representation(snapshot.bmain,*copy_material,0,*color,true,true);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);value=old;
 // Validate the cold contract from the same resource pass and typed document.
 TextureDocument copied=document;copied.set(summary.session_id).material_session_uid=copy_material->id.session_uid;assert(validate_packed_material_layer_sources(snapshot.bmain,*copy_scene,copied,resources));
 {auto *node=bke::node_find_node_by_name(*copy_material->nodetree,"__BTS_LayerMaterial0ColorTap3");const auto id=node->id;node->id=nullptr;rejected=false;try{validate_packed_material_layer_sources(snapshot.bmain,*copy_scene,copied,resources);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);node->id=id;}
 // Real disk round-trip: a version-2 graph and one original byte payload,
 // then reconstruct two typed aliases without any original path/cache.
 QTemporaryDir output_dir;assert(output_dir.isValid());const auto path=output_dir.filePath("srgb-one-original.blend");ReportList reports{};BKE_reports_init(&reports,RPT_STORE);const auto cleanup=qScopeGuard([&]{BKE_reports_free(&reports);});BlendFileWriteParams write{};write.use_save_as_copy=true;assert(BLO_write_file(&snapshot.bmain,path.toUtf8().constData(),0,&write,&reports));
 QFile disk(path);assert(disk.open(QIODevice::ReadOnly)&&disk.readAll().count(original.bytes)==1);BlendFileReadReport read_report{};read_report.reports=&reports;std::unique_ptr<BlendFileData,decltype(&BLO_blendfiledata_free)> read(BLO_read_from_file(path.toUtf8().constData(),BLO_READ_SKIP_USERDEF,&read_report),BLO_blendfiledata_free);assert(read&&read->main);
 auto &cold=*read->main;auto &cold_scene=*static_cast<Scene *>(cold.scenes.first);auto &cold_material=*static_cast<Material *>(cold.materials.first);ProjectResourceImages cold_resources;const auto cold_assets=read_project_assets(cold_scene,&cold_resources);assert(cold_assets.size()==1&&cold_resources.by_key.size()==1);
 const std::array<TextureSetBinding,1> cold_bindings{{{set.id,cold_material.id.session_uid,set.name}}};auto restored=TextureDocument::from_json(before,cold_bindings);resolve_fill_textures(restored,cold_assets);assert(validate_packed_material_layer_sources(cold,cold_scene,restored,cold_resources));
 auto &restored_set=restored.set(cold_material.id.session_uid);MaterialLayerImages cold_images{};
 for(int i=0;i<2;++i){const auto &source=restored_set.material_layering.materials[i].textures[i?2:0];auto *image=reinterpret_cast<Image *>(BKE_id_new(&cold,ID_IM,"sRGB reopened typed alias"));id_us_min(&image->id);describe_source(*image,2,2);Buffer header(material_layer_source_pixel_view(*source),IMB_freeImBuf);assert(BKE_image_assign_hosted_immutable_source(image,1001,header.get()));cold_images[i][i?2:0]=image;}
 MaterialLayerShader reopened;reopened.apply(cold,cold_material,restored_set.material_layering,1,1,false,cold_images);const auto reopened_bindings=capture_material_layer_source_bindings(cold,restored);assert(reopened_bindings.size()==2);
 for(auto &layer:cold_scene.view_layers)BKE_view_layer_synced_ensure(cold,&cold_scene,&layer);bke::blendfile::PartialWriteContext again(cold);again.bmain.colorspace=cold.colorspace;auto *again_scene=reinterpret_cast<Scene *>(again.id_add(&cold_scene.id,{Context::ADD_DEPENDENCIES}));again.id_add(&cold_material.id,{Context::ADD_DEPENDENCIES});auto again_owners=retain_material_layer_source_snapshot_headers(cold,again.bmain);ProjectResourceImages again_resources;pack_project_assets(again.bmain,*again_scene,cold_assets,&again_resources,&again);pack_material_layer_source_bindings(again,*again_scene,reopened_bindings,again_resources);assert(again.is_valid()&&again_resources.by_key.size()==1);
 // Fresh-inspection migration of a runtime graph uses its retained typed
 // provenance, not an assumption that every fresh shader sees encoded stock.
 {MaterialLayerShader fresh;fresh.apply(*live,*material,set.material_layering,1,1,false,images);assert(capture().size()==2);}
 // Same original byte encoding, newly interpreted linear Rec.709. Both
 // directions must retire/recreate the exact family without losing raw uses.
 const auto linear=decode_fill_texture(original,false,"Linear Rec.709");auto *linear_image=reinterpret_cast<Image *>(BKE_id_new(live.get(),ID_IM,"linear transition alias"));id_us_min(&linear_image->id);describe_source(*linear_image,2,2);{Buffer header(material_layer_source_pixel_view(*linear),IMB_freeImBuf);assert(BKE_image_assign_hosted_immutable_source(linear_image,1001,header.get()));}
 set.material_layering.materials[0].textures[0]=linear;images[0][0]=linear_image;shader.apply(*live,*material,set.material_layering,1,1,false,images);assert(capture().size()==2&&!bke::node_find_node_by_name(tree,"__BTS_LayerMaterial0SrgbMixed"));
 set.material_layering.materials[0].textures[0]=color;images[0][0]=aliases[0];shader.apply(*live,*material,set.material_layering,1,1,false,images);assert(capture().size()==2&&bke::node_find_node_by_name(tree,"__BTS_LayerMaterial0SrgbMixed"));
 set.material_layering.materials[0].rotation=30;set.material_layering.materials[0].offset={.125,-.125};shader.apply(*live,*material,set.material_layering,1,1,false,images);assert(capture().size()==2);
 set.material_layering.enabled=false;shader.apply(*live,*material,set.material_layering,1,1,false,{});assert(capture().empty()&&!bke::node_find_node_by_name(tree,"__BTS_LayerMaterial0SrgbMixed"));
 set.material_layering.enabled=true;shader.apply(*live,*material,set.material_layering,1,1,false,images);assert(capture().size()==2);
 // Switching to an ordinary linear/raw path retires only the validated owned
 // nonlinear filter, retaining data source IDs and the legacy linear sampler.
 set.material_layering.materials[0].textures[0].reset();images[0][0]=nullptr;shader.apply(*live,*material,set.material_layering,1,1,false,images);for(const auto &node:tree.nodes)assert(!STRPREFIX(node.name,"__BTS_LayerMaterial0Srgb")&&!STRPREFIX(node.name,"__BTS_LayerMaterial0ColorTap"));assert(images[1][2]==aliases[1]);
 return rejected_graphs;
}
