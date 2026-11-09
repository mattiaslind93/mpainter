// SPDX-License-Identifier: GPL-3.0-or-later
// Retain the existing strict raw bitmap/scalar-Levels fixture builders.
#pragma push_macro("main")
#undef main
#define main spsm_scalar_levels_previous_main
#include "spsm_bitmap_levels_test.cc"
#pragma pop_macro("main")
#include "OrderedRawRgbFixture.hh"
#include "SourceCpuRegression.hh"
#include "SpsmSource.hh"
namespace {
// Mutations replace whole subtrees. Repack the synthetic arena and references
// exactly as the structural reader does; no unreachable or aliased test nodes.
void compact_color_fixture(s::Archive &archive,Builder *builder=nullptr){
 auto &p=archive.presets[0];const auto old=std::move(p.nodes);std::vector<std::optional<s::NodeId>> remap(old.size());
 std::function<s::NodeId(s::NodeId)> visit=[&](s::NodeId id){require(id<old.size()&&!remap[id],"Synthetic Color tree aliases a node");auto n=old[id];const auto next=p.nodes.size();remap[id]=next;p.nodes.push_back(n);if(auto o=std::get_if<s::Object>(&n.value))for(auto &f:o->fields)f.value=visit(f.value);if(auto a=std::get_if<s::Array>(&n.value))for(auto &child:a->elements)child=visit(child);p.nodes[next]=std::move(n);return next;};
 p.nodes.clear();p.root=visit(p.root);
 std::vector<s::Reference> refs;for(auto ref:archive.references)if(ref.preset!=0||remap.at(ref.source)){if(ref.preset==0)ref.source=*remap[ref.source];refs.push_back(std::move(ref));}archive.references=std::move(refs);
 if(builder)for(auto *id:{&builder->url,&builder->src,&builder->fill,&builder->leaf,&builder->root})*id=remap.at(*id).value_or(0);
}
s::Archive color_fixture(const s::Archive &image,Builder &builder,unsigned mutation=0){
 auto archive=builder.build(image);auto &p=archive.presets[0];
 const auto append=[&](auto value){auto id=p.nodes.size();p.nodes.push_back({18,0,1,std::move(value)});return id;};
 // Independent Color/Roughness source nodes deliberately share one raw record.
 std::function<s::NodeId(s::NodeId)> clone=[&](s::NodeId id){
  auto n=p.nodes.at(id);if(auto o=std::get_if<s::Object>(&n.value))for(auto &f:o->fields)f.value=clone(f.value);if(auto a=std::get_if<s::Array>(&n.value))for(auto &v:a->elements)v=clone(v);
  const auto next=p.nodes.size();p.nodes.push_back(std::move(n));
  const auto refs=archive.references;for(auto ref:refs)if(ref.preset==0&&ref.source==id){ref.source=next;archive.references.push_back(std::move(ref));}
  return next;
 };
 const auto color_source=clone(builder.src);
 const auto color_bit=append(std::uint64_t(1));
 for(auto &f:std::get<s::Object>(p.nodes[color_source].value).fields)if(f.name=="channelTypes")f.value=color_bit;
 const auto sources=jf(p,builder.fill,"sources");std::get<s::Array>(p.nodes[sources].value).elements.push_back(color_source);
 auto &curves=std::get<s::Array>(p.nodes[jf(p,jf(p,builder.leaf,"actions"),"items")].value).elements;
 const auto color_action=curves[1],curve=jf(p,color_action,"levels");
 std::get<s::FloatValues>(p.nodes[jf(p,curve,"gamma")].value).values={.75f,1.5f,2};
 std::get<s::FloatValues>(p.nodes[jf(p,curve,"inputMinimum")].value).values={-.25f,.1f,-.125f};
 std::get<s::FloatValues>(p.nodes[jf(p,curve,"outputMaximum")].value).values={1.5f,.8f,1.25f};
 // A separate scalar Roughness Levels action follows Color Levels.
 const auto rough_action=clone(color_action);const auto rough_bit=append(std::uint64_t(128));
 for(auto &f:std::get<s::Object>(p.nodes[rough_action].value).fields)if(f.name=="channelTypes")f.value=rough_bit;
 const auto rough_curve_id=jf(p,rough_action,"levels");
 auto scalar_curve=std::get<s::Object>(p.nodes[rough_curve_id].value);
 for(auto &f:scalar_curve.fields){if(f.name=="gamma")f.value=append(s::FloatValues{{1.75f,1.75f,1.75f},3});if(f.name=="inputMinimum")f.value=append(s::FloatValues{{0,0,0},3});if(f.name=="outputMaximum")f.value=append(s::FloatValues{{1,1,1},3});}
 p.nodes[rough_curve_id].value=std::move(scalar_curve);
 // Reacquire after appending nodes: node-vector growth invalidates references.
 std::get<s::Array>(p.nodes[jf(p,jf(p,builder.leaf,"actions"),"items")].value).elements.push_back(rough_action);
 if(mutation==1)std::get<s::FloatValues>(p.nodes[jf(p,curve,"gamma")].value).values[2]=0;
 if(mutation==2)std::get<s::FloatValues>(p.nodes[jf(p,rough_curve_id,"gamma")].value).values[1]=2;
 if(mutation==3){const auto selection=append(s::Blob{0,1});std::get<s::Object>(p.nodes[color_action].value).fields.push_back({"selection",selection});}
 if(mutation==4){auto &items=std::get<s::Array>(p.nodes[jf(p,jf(p,builder.leaf,"actions"),"items")].value).elements;std::swap(items[0],items[1]);}
 if(mutation==5)std::get<std::uint64_t>(p.nodes[jf(p,builder.fill,"projection")].value)=1;
 if(mutation==6){const auto gamma=jf(p,curve,"gamma");std::get<s::FloatValues>(p.nodes[gamma].value).values[0]=std::numeric_limits<float>::quiet_NaN();}
 if(mutation==7){const auto gamma=append(true);std::get<s::Object>(p.nodes[builder.leaf].value).fields.push_back({"gammaCompensation",gamma});}
 if(mutation==8){
  const auto second_fill=clone(builder.fill);const auto second_sources=jf(p,second_fill,"sources");
  std::get<s::Array>(p.nodes[second_sources].value).elements={color_source};
  const auto second_uv=jf(p,second_fill,"uvTransformation"),scale=jf(p,second_uv,"scaleFactors");std::get<s::FloatValues>(p.nodes[scale].value).values={8,9};
  std::get<s::Array>(p.nodes[jf(p,builder.fill,"sources")].value).elements={builder.src};
  auto &items=std::get<s::Array>(p.nodes[jf(p,jf(p,builder.leaf,"actions"),"items")].value).elements;items.insert(items.begin()+1,second_fill);
 }
 compact_color_fixture(archive,&builder);return archive;
}
void color_synthetic(){
 QTemporaryDir directory;require(directory.isValid(),"Color synthetic directory failed");const auto archive=image_file(directory.filePath("rgb.spsm").toStdString(),false,true);
 SpsmBitmapTextureCache cache;unsigned reads=0;std::shared_ptr<const SpsmBitmapTexture> image;
 SpsmBitmapResolver resolve=[&](const auto &d,const auto &p,auto o){++reads;image=cache.load(d,p,o);return image;};
 Builder builder;auto original=prepare_spsm_material(sem::inspect_archive(color_fixture(archive,builder)));
 require(named(original.material,"leaf").visible==builder.enabled,"Preparation changed authored Color fixture visibility");
 auto selected=policy();selected.levels=SpsmLevelsPolicy::NativeScalarLevels;const auto matrix=ordered_rgb_test::fixed_matrix();
 const auto output=enhance_prepared_spsm_bitmaps(original,selected,resolve,{true,matrix});const auto &leaf=named(output.material,"leaf");
 require(reads==1&&leaf.visible&&leaf.channels[0]&&leaf.channels[2]&&!leaf.channels[3]&&leaf.effects.size()==3,"Combined selected channels were not restored");
 require(leaf.fill.texture==fill_channel_texture(leaf.fill,2)&&leaf.fill.texture->pixels==image->texture.pixels,"Same raw image duplicated across Color/Roughness");
 require(!leaf.fill.alpha_mask[0]&&!leaf.fill.alpha_mask[2]&&leaf.values.base_color==std::array<float,4>{1,1,1,1},"A or implicit tint changed RGB interpretation");
 require(leaf.effects[0].channel==0&&leaf.effects[1].channel==2&&leaf.effects[1].gamma==1.75f&&raw_linear_color_source(leaf),"Per-channel Levels or final matrix order changed");
 require(!fill_has_texture(named(original.material,"leaf").fill)&&original.source==output.source&&!output.appearance_verified,"Original IR/source was mutated");
 auto isolated=output.material.set;isolated.layers={leaf};isolated.layers[0].depth=0;isolated.layers[0].parent=QUuid();isolated.texture_resolution=128;isolated.precision[0]=ChannelPrecision::RGB32F;isolated.precision[2]=ChannelPrecision::L32F;
 double maximum=0;unsigned components=0;
 for(int tile:{1001,1017})for(auto xy:{std::pair(0,0),std::pair(25,81),std::pair(63,64),std::pair(127,127)}){
  std::array<double,3> rgb{};const auto &header=*image->original.encoded->header;
  for(double dx:{.25,.75})for(double dy:{.25,.75}){
   double u=((xy.first+dx)/128+(tile-1001)%10-.5)*leaf.fill.scale[0]+.5,v=((xy.second+dy)/128+(tile-1001)/10-.5)*leaf.fill.scale[1]+.5;
   u-=std::floor(u);v-=std::floor(v);const double x=u*header.width-.5,y=v*header.height-.5;const int ix=std::floor(x),iy=std::floor(y);
   for(int c=0;c<3;++c)for(int j=0;j<2;++j)for(int i=0;i<2;++i){const int tx=((ix+i)%int(header.width)+header.width)%header.width,ty=((iy+j)%int(header.height)+header.height)%header.height;
    const auto *p=image->original.bytes->data()+ty*header.pitch+tx*8+c*2;const auto value=blender::math::half_to_float(std::uint16_t(p[0]|(p[1]<<8)));
    rgb[c]+=value*(i?x-ix:1-(x-ix))*(j?y-iy:1-(y-iy))*.25;
   }
  }
  for(int c=0;c<3;++c)rgb[c]=ordered_rgb_test::curve(leaf.effects[0].rgb_curves[c],rgb[c]);
  rgb=ordered_rgb_test::matrix(*matrix,rgb);const auto actual=TextureDocument::composite(isolated,xy.first,xy.second,tile);
  for(int c=0;c<3;++c){const auto difference=std::abs(rgb[c]-actual.authored.base_color[c]);maximum=std::max(maximum,difference);require(difference<5e-6,"RGB lowering differs from independent raw-half/mapping/Levels/matrix oracle");++components;}
  const float expected_rough=joint_formula(raw_aa(*image,leaf,xy.first,xy.second,tile),0,1,1.75f,0,1,false);
  require(std::abs(expected_rough-actual.authored.roughness)<3e-6,"RGB restoration changed independent Roughness Levels");
 }
 const auto before=reads;auto rough_only=enhance_prepared_spsm_bitmaps(original,selected,resolve);require(reads==before+1&&!named(rough_only.material,"leaf").channels[0]&&named(rough_only.material,"leaf").effects.size()==1,"Default Color Unknown changed scalar lowering");
 auto color_only=enhance_prepared_spsm_bitmaps(original,selected,resolve,{false,matrix});require(named(color_only.material,"leaf").channels[0]&&!named(color_only.material,"leaf").channels[2]&&named(color_only.material,"leaf").effects.size()==2,"Color-only implied Roughness");
 // Bitmap eligibility is independent of the authored layer enabled flag.
 auto unknown=enhance_prepared_spsm_bitmaps(original,selected,resolve,{false,{}});const auto &unknown_leaf=named(unknown.material,"leaf");
 require(unknown_leaf.visible==builder.enabled&&reads==before+2&&!fill_has_texture(unknown_leaf.fill)&&unknown_leaf.channels==named(original.material,"leaf").channels&&effects_json(unknown_leaf.effects)==effects_json(named(original.material,"leaf").effects),"No selected channels changed visibility or hydrated");
 const auto held=reads;auto scalar_unknown=selected;scalar_unknown.levels=SpsmLevelsPolicy::Unknown;
 const auto deferred=enhance_prepared_spsm_bitmaps(original,scalar_unknown,resolve,{true,matrix});
 require(reads==held&&named(deferred.material,"leaf").visible==builder.enabled&&!fill_has_texture(named(deferred.material,"leaf").fill)&&named(deferred.material,"leaf").effects.empty()&&named(deferred.material,"leaf").channels==named(original.material,"leaf").channels,"Missing selected Roughness Levels policy did not defer whole leaf atomically");
 const auto color_without_scalar=enhance_prepared_spsm_bitmaps(original,scalar_unknown,resolve,{false,matrix});require(named(color_without_scalar.material,"leaf").channels[0]&&!named(color_without_scalar.material,"leaf").channels[2],"Color-only required an unselected Roughness Levels policy");
 const auto bytes=write_material_preset(output.material);const auto reopened=read_material_preset(bytes);const auto &saved=named(reopened,"leaf");
 require(saved.fill.texture&&saved.fill.texture->pixels==fill_channel_texture(saved.fill,2)->pixels&&effects_json(saved.effects)==effects_json(leaf.effects),"Preset Save/Open lost shared source/ordered interpretation");
 for(unsigned mutation=1;mutation<=8;++mutation){Builder bad;auto input=prepare_spsm_material(sem::inspect_archive(color_fixture(archive,bad,mutation)));const auto count=reads;const auto denied=enhance_prepared_spsm_bitmaps(input,selected,resolve,{true,matrix});require(reads==count&&named(denied.material,"leaf").visible==bad.enabled&&!fill_has_texture(named(denied.material,"leaf").fill)&&named(denied.material,"leaf").channels==named(input.material,"leaf").channels&&effects_json(named(denied.material,"leaf").effects)==effects_json(named(input.material,"leaf").effects),"Invalid late selected chain changed visibility or partially hydrated/published");}
 // Distinct resource records can fail on the second decode after the first
 // private cache entry exists. Neither selected channel may be published.
 {
  auto distinct=archive;const auto file=H5Fopen(distinct.source.string().c_str(),H5F_ACC_RDWR,H5P_DEFAULT);require(file>=0,"Cannot open second-resource fixture");
  require(H5Ocopy(file,"/resources/test.image",file,"/resources/other.image",H5P_DEFAULT,H5P_DEFAULT)>=0,"Cannot copy second dataset");H5Fclose(file);
  distinct.source_stamp=s::ImageSourceStamp{std::filesystem::file_size(distinct.source),std::filesystem::last_write_time(distinct.source)};distinct.datasets.push_back({"/resources/other.image",archive.datasets[0].size});
  Builder separate;auto built=color_fixture(distinct,separate);auto &p=built.presets[0];
  const auto url=p.nodes.size();p.nodes.push_back({18,0,1,std::string("resource://fixture?version=other.image")});auto bitmap=p.nodes[jf(p,separate.src,"bitmap")];for(auto &f:std::get<s::Object>(bitmap.value).fields)if(f.name=="urlToBitmapRes")f.value=url;
  const auto bitmap_id=p.nodes.size();p.nodes.push_back(std::move(bitmap));for(auto &f:std::get<s::Object>(p.nodes[separate.src].value).fields)if(f.name=="bitmap")f.value=bitmap_id;
  built.references.push_back({0,url,"urlToBitmapRes","resource://fixture?version=other.image","/resources/other.image"});compact_color_fixture(built);const auto input=prepare_spsm_material(sem::inspect_archive(std::move(built)));auto snapshot=ordered_rgb_test::document();auto &tracked=snapshot.set(1);tracked=input.material.set;const auto before=snapshot.to_json();const auto source_owner=input.source;unsigned attempts=0;std::string injected_error;
  try{enhance_prepared_spsm_bitmaps(input,selected,[&](const auto &d,const auto &p,auto budget){++attempts;if(d.resource.dataset.path=="/resources/other.image")throw std::runtime_error("Injected second selected resource failure");return cache.load(d,p,budget);},{true,matrix});}catch(const std::exception &error){injected_error=error.what();}
  tracked=input.material.set;
  require(injected_error=="Injected second selected resource failure"&&attempts==2&&snapshot.to_json()==before&&input.source==source_owner&&!fill_has_texture(named(input.material,"leaf").fill),"Second selected decode failure partly published or mutated original");
 }
 std::cout<<"PASS synthetic RGB+R: explicit source-linear RGB Levels, independent scalar Levels, one raw allocation, defaultUnknown/Color-only, eight atomic no-decode failures (including distinct Fill/mapping), preset roundtrip; independent components="<<components<<" max_error="<<maximum<<" source appearance unverified\n";
}
void color_wood(const QString &directory,const QString &output){
 const auto path=directory+"/used_varnish_wood.spsm";const auto original_sha=joint_hash(path);
 auto original=prepare_spsm_material(std::filesystem::path(path.toStdString()));SpsmBitmapTextureCache cache;
 std::map<std::string,std::shared_ptr<const SpsmBitmapTexture>> images;unsigned calls=0;
 SpsmBitmapResolver resolve=[&](const auto &d,const auto &p,auto budget){++calls;auto image=cache.load(d,p,budget);images[d.resource.dataset.path]=image;return image;};
 auto selected=policy();selected.levels=SpsmLevelsPolicy::NativeScalarLevels;const auto matrix=ordered_rgb_test::fixed_matrix();
 const auto rough=enhance_prepared_spsm_bitmaps(original,selected,resolve);require(images.size()==2,"Actual baseline Roughness resources changed");
 const auto combined=enhance_prepared_spsm_bitmaps(original,selected,resolve,{true,matrix});require(images.size()==4&&calls==6,"Actual channels did not retain their four distinct records");
 struct Expected {const char *name,*color,*rough;LayerBlend blend;std::size_t effects;};
 QJsonArray bindings;
 for(const auto &expected:{Expected{"Wood_Worn_base","/resources/ebd7c2b2e295ba73b056493abb203c0236ab183d.image","/resources/56271f14721261acbab5874fa9f47a97e59ffb39.image",LayerBlend::LinearDodge,2},Expected{"Wood_Darken_base","/resources/1d94222725eb74ea0f4eeea214ad69b835e9a99f.image","/resources/44a7fb2d1313c31d50caf61e5b2e5550f2297482.image",LayerBlend::Normal,3}}){
  const auto &layer=named(combined.material,expected.name);const auto color=images.at(expected.color),scalar=images.at(expected.rough);
  require(layer.visible&&layer.channels[0]&&layer.channels[2]&&layer.effects.size()==expected.effects&&raw_linear_color_source(layer),"Actual selected channel/effect order missing");
  require(layer.fill.texture==color->binding()&&fill_channel_texture(layer.fill,2)==scalar->binding()&&layer.fill.texture!=fill_channel_texture(layer.fill,2),"Distinct actual Color/Roughness sources were aliased");
  require(fill_channel_texture(layer.fill,2)==fill_channel_texture(named(rough.material,expected.name).fill,2),"Color opt-in copied or replaced existing Roughness owner");
  require(layer.opacity[0]==.550000011920929f&&layer.blending[0]==expected.blend&&layer.values.base_color==std::array<float,4>{1,1,1,1},"Actual Color opacity/blend/neutral canonical tint changed");
  require(effects_json(layer.effects)[0].toObject()["channel"]==0&&layer.effects.back().linear_color==matrix,"Actual RGB Levels/matrix order changed");
  const auto metadata=[](const auto &image){return QJsonObject{{"dataset",QString::fromStdString(image->source.dataset.path)},{"encoded_sha256",QString::fromStdString(image->encoded_sha256)},{"pixel_hash",QString::fromStdString(image->texture.hash)},{"asset",QString::fromStdString(image->texture.asset)},{"width",image->texture.width},{"height",image->texture.height}};};
  QJsonArray owners;std::set<s::NodeId> owning_fills;
  const auto &inspection=*original.source;const auto pi=*inspection.primary_preset;const auto &preset=inspection.source().presets[pi];const auto &ir=inspection.presets[pi];
  const auto source_layer=std::find_if(original.layers.begin(),original.layers.end(),[&](const auto &binding){return binding.native_layer==layer.id;});require(source_layer!=original.layers.end(),"Actual native/source layer binding missing");
  for(const auto &source:ir.layers)if(source.source==source_layer->source)for(auto index:source.actions){const auto &action=ir.actions.at(index);if(action.kind!=sem::ActionKind::Fill)continue;
   for(auto id:action.sources){const auto found=std::find_if(ir.sources.begin(),ir.sources.end(),[&](const auto &s){return s.source==id;});if(found==ir.sources.end()||!found->bitmap)continue;
    const auto url=jf(preset,*found->bitmap,"urlToBitmapRes");for(const auto &ref:inspection.source().references)if(ref.preset==pi&&ref.source==url&&(ref.resource_dataset==expected.color||ref.resource_dataset==expected.rough)){
     owning_fills.insert(action.source);owners.append(QJsonObject{{"dataset",QString::fromStdString(ref.resource_dataset)},{"source_node",qint64(id)},{"owning_fill_node",qint64(action.source)}});
    }
   }
  }
  require(owners.size()==2&&owning_fills.size()==1,"Actual selected channels no longer belong to one validated Fill");
  bindings.append(QJsonObject{{"layer",QString::fromLatin1(expected.name)},{"color",metadata(color)},{"roughness",metadata(scalar)},{"source_owners",owners}});
 }
 const auto &parent=named(combined.material,"Wood_Color_Roughness_base");require(parent.mask&&parent.mask->enabled&&parent.mask->background==0&&parent.mask->effects.empty(),"Actual ancestor mask0 was changed");
 for(auto xy:{std::pair(0,0),std::pair(63,64),std::pair(127,127)}){auto before=original.material.set,after=combined.material.set;before.texture_resolution=after.texture_resolution=128;const auto a=TextureDocument::composite(before,xy.first,xy.second),b=TextureDocument::composite(after,xy.first,xy.second);for(int c=0;c<3;++c)require(a.material.base_color[c]==b.material.base_color[c],"Actual zero ancestry was bypassed");require(a.coverage[0]==b.coverage[0]&&a.material.roughness==b.material.roughness,"Actual ancestor mask semantics changed");}
 require(original_sha==joint_hash(path)&&combined.source==original.source&&!combined.appearance_verified,"Original source changed or appearance claim made");
 if(!output.isEmpty()){QFile file(output);require(file.open(QIODevice::WriteOnly),"Cannot write actual per-channel provenance");file.write(QJsonDocument(QJsonObject{{"source_sha256",QString::fromLatin1(original_sha.toHex())},{"bindings",bindings},{"distinct_raw_records",4},{"same_record_sharing_proven_separately",true},{"original_ancestor_mask0_retained",true},{"appearance_verified",false}}).toJson());}
 std::cout<<"PASS actual Wood selected native RGB+Levels: four distinct datasets, Roughness owners/effects retained, Color Add/Normal opacity.55, original ancestor0, original SHA="<<original_sha.toHex().toStdString()<<" appearance unverified\n";
}
}
void bts::run_spsm_color_lowering_cpu_regression(){
 // This parser boundary is fixture-free. Source/bitmap bytes are not read.
 QStringList tags{"spsm:policy:bitmap-roughness=unknown","spsm:policy:bitmap-bytes=little-endian","spsm:policy:bitmap-rows=first-row-v0","spsm:policy:bitmap-alpha=data-components","spsm:policy:bitmap-uv=projection0-centered-repeat-factors","spsm:policy:bitmap-sampling=hq-repeat","spsm:policy:bitmap-color=unknown"};
 const auto unknown=parse_spsm_source_policy(tags);require(unknown.bitmap_color==SpsmBitmapColorInput::Unknown&&!unknown.roughness_raw_r_ignore_a&&unknown.bitmaps.uv==SpsmBitmapUV::Unknown,"Default Unknown hydrated/interpreted Color");
 tags.back()="spsm:policy:bitmap-color=native-linear-rec709-rgb-levels-ignore-a";const auto color=parse_spsm_source_policy(tags);require(color.bitmap_color==SpsmBitmapColorInput::NativeLinearRec709RGBLevelsIgnoreA&&!color.roughness_raw_r_ignore_a&&color.bitmaps.uv==SpsmBitmapUV::Projection0CenteredRepeatFactors,"Color-only lost UV or implied Roughness");
 tags.back()="spsm:policy:bitmap-color=native-acescg-rgb-levels-ignore-a";require(parse_spsm_source_policy(tags).bitmap_color==SpsmBitmapColorInput::NativeACEScgRGBLevelsIgnoreA,"Explicit ACEScg policy lost");
 color_synthetic();
}
#ifndef BTS_EMBED_SOURCE_REGRESSIONS
int main(int argc,char **argv){QCoreApplication app(argc,argv);std::cout<<std::unitbuf;try{synthetic();joint_synthetic();color_synthetic();if(argc>1)color_wood(QString::fromUtf8(argv[1]),argc>2?QString::fromUtf8(argv[2]):QString{});return 0;}catch(const std::exception &error){std::cerr<<"SPSM Color lowering FAIL "<<error.what()<<'\n';return 1;}}

#endif
