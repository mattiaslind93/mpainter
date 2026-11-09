// SPDX-License-Identifier: GPL-3.0-or-later
#define main spsm_existing_joint_test_main
#include "spsm_bitmap_levels_test.cc"
#undef main
#include "SpsmSource.hh"
#include "SpsmAssetDispatch.hh"
#include "IMB_imbuf.hh"
#include "CLG_log.h"
#include <future>
namespace {
s::NodeId ma(s::Preset &p,s::Value v){auto id=p.nodes.size();p.nodes.push_back({18,0,1,std::move(v)});return id;}
s::NodeId mo(s::Preset &p,std::string n,std::vector<s::Field> f){return ma(p,s::Object{std::move(n),std::move(f)});}
void mf(s::Preset &p,s::NodeId id,std::string key,s::NodeId value){auto &all=std::get<s::Object>(p.nodes.at(id).value).fields;for(auto &f:all)if(f.name==key){f.value=value;return;}all.push_back({std::move(key),value});}
s::NodeId clone_mask(s::Preset &p,s::NodeId id){auto n=p.nodes.at(id);if(auto o=std::get_if<s::Object>(&n.value))for(auto &f:o->fields)f.value=clone_mask(p,f.value);if(auto a=std::get_if<s::Array>(&n.value))for(auto &v:a->elements)v=clone_mask(p,v);return ma(p,std::move(n.value));}
PreparedSpsm mask_prepared(s::Archive a){
 // Replacing test fields leaves old fixture nodes disconnected. Compact only
 // this synthetic arena and preserve resource-reference node identities.
 for(std::size_t pi=0;pi<a.presets.size();++pi){auto &p=a.presets[pi];std::map<s::NodeId,s::NodeId> remap;std::vector<s::Node> nodes;
  std::function<s::NodeId(s::NodeId)> visit=[&](s::NodeId id){if(auto it=remap.find(id);it!=remap.end())return it->second;auto copy=p.nodes.at(id);const auto next=nodes.size();remap[id]=next;nodes.push_back(copy);if(auto o=std::get_if<s::Object>(&copy.value))for(auto &f:o->fields)f.value=visit(f.value);if(auto v=std::get_if<s::Array>(&copy.value))for(auto &id:v->elements)id=visit(id);nodes[next]=std::move(copy);return next;};
  p.root=visit(p.root);p.nodes=std::move(nodes);std::erase_if(a.references,[&](auto &r){return r.preset==pi&&!remap.contains(r.source);});for(auto &r:a.references)if(r.preset==pi)r.source=remap.at(r.source);
 }return prepare_spsm_material(sem::inspect_archive(std::move(a)));
}
struct MaskIds {s::NodeId layer,content,fill,source,levels,curve,group,group_stack;};
MaskIds mask_archive(s::Archive &a,const Builder &b){
 auto &p=a.presets[0];auto content=b.fill;
 mf(p,b.src,"opacity",ma(p,s::FloatValues{{1},1}));mf(p,content,"perChannelBlending",ma(p,s::Array{}));
 mf(p,b.leaf,"perChannelBlending",ma(p,s::Array{}));mf(p,b.leaf,"maskInitial",ma(p,s::FloatValues{{0},1}));
 auto fill=clone_mask(p,content);auto src=std::get<s::Array>(p.nodes.at(jf(p,fill,"sources")).value).elements[0];
 mf(p,src,"channelTypes",ma(p,std::uint64_t(1)));mf(p,fill,"channelTypes",ma(p,std::uint64_t(0)));
 auto url=jf(p,jf(p,src,"bitmap"),"urlToBitmapRes");auto ref=a.references.front();ref.source=url;a.references.push_back(ref);
 // Explicit canonical present grid exercises the shared 3D-default validator.
 std::vector<s::NodeId> vertices;
 for(int y=0;y<3;++y)for(int x=0;x<3;++x)vertices.push_back(mo(p,"DataGridVertex",{
 {"position3D",ma(p,s::FloatValues{{float(x-1),float(y-1),0},3})},{"positionUV",ma(p,s::FloatValues{{x*.5f,y*.5f},2})},
 {"tangentColumnNext",ma(p,s::FloatValues{{x==2?0.f:1.f/3,0,0},3})},{"tangentColumnPrev",ma(p,s::FloatValues{{x==0?0.f:-1.f/3,0,0},3})},
 {"tangentRowNext",ma(p,s::FloatValues{{0,y==2?0.f:1.f/3,0},3})},{"tangentRowPrev",ma(p,s::FloatValues{{0,y==0?0.f:-1.f/3,0},3})}}));
 auto grid=mo(p,"DataGridDeformation",{{"depth",ma(p,s::FloatValues{{.1f},1})},{"gridSize",ma(p,s::IntValues{{3,3},2})},{"vertices",ma(p,s::Array{std::move(vertices)})}});mf(p,fill,"dddGrid",grid);
 auto group_stack=mo(p,"DataStackActions",{{"items",ma(p,s::Array{})}});
 auto blend=mo(p,"DataBlending",{{"channelTypes",ma(p,UINT64_MAX)},{"blendingMode",ma(p,std::uint64_t(25))},{"opacity",ma(p,s::FloatValues{{1},1})}});
 auto group=mo(p,"DataActionGroup",{{"channelTypes",ma(p,std::uint64_t(0))},{"enabled",ma(p,true)},{"perChannelBlending",ma(p,s::Array{{blend}})},{"subStack",group_stack}});
 auto scalar=[&](float v){return ma(p,s::FloatValues{{v,v,v},3});};
 auto curve=mo(p,"DataLevels",{{"clamp",ma(p,std::uint64_t(0))},{"gamma",scalar(2)},{"inputMinimum",scalar(.3f)},{"inputMaximum",scalar(.8f)},{"outputMinimum",scalar(0)},{"outputMaximum",scalar(1)}});
 auto levels=mo(p,"DataActionFilterLevels",{{"channelTypes",ma(p,std::uint64_t(0))},{"enabled",ma(p,true)},{"levels",curve}});
 auto stack=mo(p,"DataStackActions",{{"items",ma(p,s::Array{{group,fill,levels}})}});mf(p,b.leaf,"maskActions",stack);
 return {b.leaf,content,fill,src,levels,curve,group,group_stack};
}
SpsmBitmapLoweringPolicy mask_policy(){auto o=policy();o.mask=SpsmMaskBitmapPolicy::NativeOrderedFillLevelsRawRIgnoreA;return o;}
float mask_raw(const SpsmBitmapTexture &image,const FillProjection &projection,int x,int y){float r=0;for(double dx:{.25,.75})for(double dy:{.25,.75})r+=raw_sample(image,((x+dx)/128-.5)*projection.scale[0]+.5,((y+dy)/128-.5)*projection.scale[1]+.5)*.25f;return r;}
void mask_oracle(const TextureLayer &l,const SpsmBitmapTexture &image){require(l.mask&&l.mask->enabled&&l.mask->background==0&&l.mask->effects.size()==2,"Mask metadata lost");const auto &m=*l.mask;require(m.effects[0].kind==MaskEffectKind::Fill&&m.effects[1].kind==MaskEffectKind::Levels,"Mask order lost");const auto &e=m.effects[1].levels;float error=0;for(auto xy:{std::pair(0,0),std::pair(25,81),std::pair(63,64),std::pair(127,127)}){const auto raw=mask_raw(image,m.effects[0].mapping.projection,xy.first,xy.second);const auto expected=std::clamp(joint_formula(raw,e.input_low,e.input_high,e.gamma,e.output_low,e.output_high,e.clamp),0.f,1.f);const auto actual=m.value(xy.first,xy.second,1001,128);error=std::max(error,std::abs(expected-actual));require(std::abs(expected-actual)<3e-6,"Native mask differs from independent raw-half Levels/final coverage oracle");}std::cout<<"PASS mask raw/Levels/finalcoverage error="<<error<<'\n';}
void mask_synthetic(){
 QTemporaryDir dir;auto archive=image_file(dir.filePath("mask.spsm").toStdString(),false,true);Builder b;b.levels=0;auto a=b.build(archive);const auto ids=mask_archive(a,b);auto original=mask_prepared(a);
 SpsmBitmapTextureCache cache;unsigned calls=0;std::shared_ptr<const SpsmBitmapTexture> image;
 const SpsmBitmapResolver resolver=[&](auto &d,auto &p,auto budget){++calls;return image=cache.load(d,p,budget);};
 auto unknown=enhance_prepared_spsm_bitmaps(original,policy(),resolver);require(!calls&&!named(unknown.material,"leaf").visible&&has(unknown,"bitmap-mask-policy-required"),"Missing mask optin decoded/enabled");
 auto out=enhance_prepared_spsm_bitmaps(original,mask_policy(),resolver);const auto &l=named(out.material,"leaf");require(calls==1&&l.visible&&l.channels[2]&&l.mask&&l.mask->effects.size()==2,"Atomic mask branch not restored");require(fill_channel_texture(l.fill,2)==l.mask->effects[0].mapping.projection.texture,"Content and mask have duplicate pixel owners");require(image->texture.minimum==-1&&image->texture.maximum==2,"Signed/HDR test raw R altered");mask_oracle(l,*image);
 auto saved=write_material_preset(out.material);auto reopened=read_material_preset(saved);mask_oracle(named(reopened,"leaf"),*image);require(fill_channel_texture(named(reopened,"leaf").fill,2)->pixels==named(reopened,"leaf").mask->effects[0].mapping.projection.texture->pixels,"Native archive did not dedup mask/content pixels");
 for(int mode=0;mode<38;++mode){auto bad=a;auto &p=bad.presets[0];const auto start=calls;
  if(mode==0)mf(p,ids.fill,"futureControl",ma(p,true));
  if(mode==1)mf(p,ids.fill,"uvTransferChannel",ma(p,std::uint64_t(1)));
  if(mode==2)mf(p,ids.fill,"uvGrid",ma(p,true));
  if(mode==3)mf(p,ids.fill,"dddSize",ma(p,s::FloatValues{{1,2,1},3}));
  if(mode==4)mf(p,ids.fill,"uvSamplingWrap",ma(p,std::uint64_t(0)));
  if(mode==5)mf(p,ids.fill,"filtering",ma(p,std::uint64_t(0)));
  if(mode==6)mf(p,ids.fill,"depthCullingHardness",ma(p,s::FloatValues{{.4f},1}));
  if(mode==7)mf(p,ids.fill,"channelTypes",ma(p,std::uint64_t(128)));
  if(mode==8)mf(p,ids.source,"opacity",ma(p,s::FloatValues{{.5f},1}));
  if(mode==9)mf(p,ids.source,"channelTypes",ma(p,std::uint64_t(129)));
  if(mode==10)mf(p,ids.curve,"futureGamma",ma(p,true));
  if(mode==11)mf(p,ids.levels,"channelTypes",ma(p,std::uint64_t(128)));
  if(mode==12)mf(p,ids.curve,"clamp",ma(p,std::uint64_t(2)));
  if(mode==13)mf(p,ids.group_stack,"items",ma(p,s::Array{{clone_mask(p,ids.levels)}}));
  if(mode==14)mf(p,ids.group,"futureIsolation",ma(p,true));
  if(mode==15)mf(p,ids.content,"futureControl",ma(p,true));
  if(mode==16)mf(p,jf(p,ids.source,"bitmap"),"alphaType",ma(p,std::uint64_t(1)));
  if(mode==17)mf(p,jf(p,ids.fill,"uvTransformation"),"GUIScaleRatioLocked",ma(p,false));
  if(mode==18){const auto grid=jf(p,ids.fill,"dddGrid");const auto v=std::get<s::Array>(p.nodes.at(jf(p,grid,"vertices")).value).elements[0];mf(p,v,"positionUV",ma(p,s::FloatValues{{.1f,0},2}));}
  if(mode==19)mf(p,jf(p,ids.fill,"dddGrid"),"depth",ma(p,s::FloatValues{{.2f},1}));
  if(mode==20)mf(p,jf(p,ids.fill,"dddGrid"),"futureGrid",ma(p,true));
  if(mode==21){const auto c=mo(p,"DataColorSpaceOverride",{{"main",ma(p,std::uint64_t(1))}});mf(p,jf(p,ids.source,"bitmap"),"colorSpaceOverride",c);}
  if(mode==22){auto blend=std::get<s::Array>(p.nodes.at(jf(p,ids.group,"perChannelBlending")).value).elements[0];mf(p,blend,"opacity",ma(p,s::FloatValues{{.5f},1}));}
  if(mode==23)mf(p,ids.fill,"selection",ma(p,s::Blob{0,1}));
  if(mode==24)mf(p,ids.layer,"gammaCompensation",ma(p,true));
  if(mode==25)mf(p,ids.source,"componentSwizzle",ma(p,std::uint64_t(1)));
  if(mode==26)mf(p,jf(p,ids.layer,"maskActions"),"futureStack",ma(p,true));
  if(mode==27)mf(p,ids.group,"tags",ma(p,std::uint64_t(1)));
  if(mode==28)mf(p,ids.levels,"enabled",ma(p,false));
  if(mode==29){auto &items=std::get<s::Array>(p.nodes.at(jf(p,jf(p,ids.layer,"maskActions"),"items")).value).elements;std::swap(items[0],items[1]);}
  if(mode==30){auto extra=clone_mask(p,ids.fill);std::get<s::Array>(p.nodes.at(jf(p,jf(p,ids.layer,"maskActions"),"items")).value).elements.push_back(extra);}
  if(mode==31){auto blend=std::get<s::Array>(p.nodes.at(jf(p,ids.group,"perChannelBlending")).value).elements[0];mf(p,blend,"blendingMode",ma(p,std::uint64_t(2)));}
  if(mode==32)mf(p,ids.curve,"gamma",ma(p,s::FloatValues{{1,2,1},3}));
  if(mode==33){auto extra=clone_mask(p,b.src);std::get<s::Array>(p.nodes.at(jf(p,ids.content,"sources")).value).elements.push_back(extra);}
  if(mode==34)mf(p,ids.layer,"enabledMeshList",ma(p,s::Array{{ma(p,std::string("unexpected-mesh"))}}));
  if(mode==35)mf(p,b.root,"futureAncestorMask",ma(p,true));
  if(mode==36){auto other=mo(p,"DataFutureUnknown",{});std::get<s::Array>(p.nodes.at(jf(p,jf(p,ids.layer,"maskActions"),"items")).value).elements.push_back(other);}
  if(mode==37){auto other=mo(p,"DataFutureUnknown",{});auto stack=mo(p,"DataStackActions",{{"items",ma(p,s::Array{{other}})}});mf(p,b.root,"maskActions",stack);}
  auto denied=enhance_prepared_spsm_bitmaps(mask_prepared(std::move(bad)),mask_policy(),resolver);
  require(calls==start&&!named(denied.material,"leaf").visible&&named(denied.material,"leaf").mask->effects.empty(),"Unknown/mutated mask field decoded or partly restored");
 }
 auto low=mask_policy();enhance_prepared_spsm_bitmaps(original,low,resolver);require(calls>1,"Former memory policy skipped resolver");
 require(!named(original.material,"leaf").visible&&named(original.material,"leaf").mask->effects.empty(),"Input mutated");
 std::cout<<"PASS mask Unknown/no-read; 38 strict atomic denials; one shared decode; native archive owner dedup; lowRAM\n";
}
void mask_wood(const QString &dir){
 const auto path=dir+"/used_varnish_wood.spsm";auto sha=joint_hash(path);auto original=prepare_spsm_material(std::filesystem::path(path.toStdString()));SpsmBitmapTextureCache cache;std::map<std::string,std::shared_ptr<const SpsmBitmapTexture>> images;
 auto resolver=[&](auto &d,auto &p,auto budget){auto image=cache.load(d,p,budget);images[d.resource.dataset.path]=image;return image;};
 auto old=enhance_prepared_spsm_bitmaps(original,policy(),resolver);auto out=enhance_prepared_spsm_bitmaps(original,mask_policy(),resolver);const auto &l=named(out.material,"Wood_base_XtraRoughness");require(l.visible&&l.channels[2]&&!l.channels[0]&&l.effects.empty()&&l.mask->effects.size()==2,"Real Xtra Roughness branch not restored");
 const auto image=images.at("/resources/56271f14721261acbab5874fa9f47a97e59ffb39.image");const auto &worn=named(out.material,"Wood_Worn_base");const auto binding=fill_channel_texture(worn.fill,2);require(binding==fill_channel_texture(l.fill,2)&&binding==l.mask->effects[0].mapping.projection.texture&&binding==fill_channel_texture(named(old.material,"Wood_Worn_base").fill,2)&&images.size()==1,"Three actual Worn bindings do not share one owner");mask_oracle(l,*image);
 const auto &ancestor=named(out.material,"Wood_Color_Roughness_base");require(ancestor.mask->background==0&&ancestor.mask->effects.empty(),"Ancestor0 unmasked");
 require(out.source==original.source&&sha==joint_hash(path)&&!out.appearance_verified,"Source/provenance changed");
 auto saved=write_material_preset(out.material);auto reopened=read_material_preset(saved);const auto &rl=named(reopened,"Wood_base_XtraRoughness");mask_oracle(rl,*image);const auto wb=fill_channel_texture(named(reopened,"Wood_Worn_base").fill,2);require(wb->pixels==fill_channel_texture(rl.fill,2)->pixels&&wb->pixels==rl.mask->effects[0].mapping.projection.texture->pixels,"Archive lost three binding owner identity");
 std::cout<<"PASS REAL Wood Xtra atomic bitmap mask; 3bindings/1immutableowner, sourceSHA="<<sha.toHex().toStdString()<<" ancestor0 retained appearance_verified=false\n";
}
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);std::cout<<std::unitbuf;try{if(argc>1&&std::string_view(argv[1])=="--runtime-baseline")return 0;synthetic();joint_synthetic();mask_synthetic();if(argc>1){fixtures(argv[1]);joint_wood(argv[1]);mask_wood(argv[1]);}return 0;}catch(const std::exception &e){std::cerr<<"SPSM bitmap mask failed: "<<e.what()<<'\n';return 1;}}
