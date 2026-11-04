// Pure syntax/identity codec tests. Encoded-image/decoder/relocation cases run in
// the host source-pixels fixture, not through fake image bytes here.
void verify_material_parameter_codec(){
 MaterialLayerParameters value;value.name="Portable four inputs";value.working_space="fixture linear context";value.scene_linear_to_xyz={1,0,0,0,1,0,0,0,1};value.tiles={{1001,{128,128}},{1002,{128,128}}};value.shader.ao_intensity=.75f;
 for(int i=0;i<4;++i){auto &input=value.materials[i];input.name="Input "+std::to_string(i);input.values.metallic=i/4.f;input.values.roughness=(4-i)/4.f;input.tiling={1.+i,.5+i};input.offset={i/8.,-i/16.};input.rotation=15*i;}
 for(int stack=0;stack<3;++stack){MaterialParameterOriginal original;original.asset=QUuid::createUuid();auto mask=std::make_shared<FillTexture>();mask->asset=original.asset.toString(QUuid::WithoutBraces).toStdString();mask->name="Opacity "+std::to_string(stack+1);mask->raw=true;mask->width=mask->height=128;auto members=std::make_shared<std::map<int,std::shared_ptr<const FillTexture>>>();
  for(int tile:{1001,1002}){auto member=std::make_shared<FillTexture>(*mask);member->hash=std::string(64,char('a'+stack));members->emplace(tile,member);original.files.push_back({TextureSourceFile{tile,QString("mask%1.%2.exr").arg(stack+1).arg(tile),"not-read-by-pure-codec",QString::fromStdString(member->hash),100},{}});}
  std::vector<TextureSourceFile> files;for(const auto &file:original.files)files.push_back(file.descriptor);mask->hash=texture_sources_hash(files).toStdString();mask->tiles=members;value.masks[stack]=mask;value.originals.push_back(std::move(original));
 }
 const auto json=material_layer_parameters_json(value);const auto read=read_material_layer_parameters_json(json);assert(material_layer_parameters_json(read)==json&&read.shader==value.shader);
 for(int i=0;i<4;++i)assert(layer_material_json(read.materials[i])==layer_material_json(value.materials[i]));
 for(const auto &mask:read.masks){assert(mask->raw&&mask->tiles->size()==2&&!mask->pixels);for(const auto &[tile,member]:*mask->tiles)assert(!member->pixels);}
 assert(!json.contains("active_mask")&&!json.contains("main_selection")&&!json.contains("material_uid"));
 auto disabled=value;disabled.enabled=false;assert(!read_material_layer_parameters_json(material_layer_parameters_json(disabled)).enabled);
 const auto reject=[](const QJsonObject &data){bool failed=false;try{read_material_layer_parameters_json(data);}catch(const std::invalid_argument &){failed=true;}assert(failed);};
 auto bad=json;bad["version"]=2;reject(bad);bad=json;bad["source_members_version"]=2;reject(bad);
 bad=json;auto masks=bad["opacity_stacks"].toArray();masks[1]=masks[0];bad["opacity_stacks"]=masks;reject(bad);
 bad=json;auto resources=bad["originals"].toArray();resources.append(resources.first());bad["originals"]=resources;reject(bad);
 bad=json;resources=bad["originals"].toArray();auto original=resources[0].toObject();auto files=original["files"].toArray();auto first=files[0].toObject();first["path"]="../outside.exr";files[0]=first;original["files"]=files;resources[0]=original;bad["originals"]=resources;reject(bad);
 bad=json;bad["scene_linear_to_xyz"]=QJsonArray{0,0,0,0,0,0,0,0,0};reject(bad);
 auto order=value;std::reverse(order.originals.begin(),order.originals.end());assert(material_layer_parameters_json(order)==json);
 TextureRamQueryScope pressure([]()->std::uint64_t{return 1;});bool rejected=false;try{material_layer_parameters_json(value);}catch(const std::length_error &){rejected=true;}assert(rejected);
}
