// SPDX-License-Identifier: GPL-3.0-or-later
// Keep the complete existing bitmap safety suite in this same native runner.
#pragma push_macro("main")
#undef main
#define main spsm_previous_bitmap_test_main
#include "spsm_bitmap_lowering_test.cc"
#pragma pop_macro("main")
#include <QCryptographicHash>
namespace {
s::NodeId jf(const s::Preset &p,s::NodeId id,std::string_view key){for(auto &f:std::get<s::Object>(p.nodes.at(id).value).fields)if(f.name==key)return f.value;throw std::runtime_error("Missing joint test field");}
void triple(s::Preset &p,s::NodeId id,float value){auto &v=std::get<s::FloatValues>(p.nodes.at(id).value);require(v.count==3,"Test triple shape");for(int c=0;c<3;++c)v.values[c]=value;}
s::NodeId rough_curve(s::Preset &p,const Builder &b){auto stack=jf(p,b.leaf,"actions");auto &items=std::get<s::Array>(p.nodes.at(jf(p,stack,"items")).value).elements;return jf(p,items.at(1),"levels");}
float joint_formula(float value,float low,float high,float gamma,float out_low,float out_high,bool clamp){double t=(double(value)-low)/(double(high)-low);if(t>0)t=std::pow(t,1/double(gamma));t=out_low+(double(out_high)-out_low)*t;t=std::clamp(t,-65504.,65504.);if(clamp)t=std::clamp(t,0.,1.);return float(t);}
float raw_aa(const SpsmBitmapTexture &image,const TextureLayer &l,int x,int y,int tile){float value=0;for(double dx:{.25,.75})for(double dy:{.25,.75}){const double u=((x+dx)/128+(tile-1001)%10-.5)*l.fill.scale[0]+.5,v=((y+dy)/128+(tile-1001)/10-.5)*l.fill.scale[1]+.5;value+=raw_sample(image,u,v)*.25f;}return std::clamp(value,0.f,1.f);}
void joint_oracle(const SmartMaterialPreset &preset,const char *name,const SpsmBitmapTexture &image,float low,float high,float gamma,float out_low,float out_high,bool clamp){
  const auto &l=named(preset,name);require(l.effects.size()==1&&l.effects[0].channel==2,"Missing scalar Levels");
  const auto &e=l.effects[0];require(e.input_low==low&&e.input_high==high&&e.gamma==gamma&&e.output_low==out_low&&e.output_high==out_high&&e.clamp==clamp,"Source Levels field mapping differs");
  auto isolated=preset.set;isolated.layers={l};isolated.layers[0].depth=0;isolated.layers[0].parent=QUuid();isolated.texture_resolution=128;isolated.texture_height=0;isolated.tile_resolutions.clear();
  float error=0;
  for(int tile:{1001,1017,2000})for(auto xy:{std::pair(0,0),std::pair(25,81),std::pair(63,64),std::pair(127,127)}){
    const float expected=joint_formula(raw_aa(image,l,xy.first,xy.second,tile),low,high,gamma,out_low,out_high,clamp);
    const auto actual=TextureDocument::composite(isolated,xy.first,xy.second,tile);
    error=std::max(error,std::abs(expected-actual.authored.roughness));
    require(std::abs(expected-actual.authored.roughness)<3e-6f,"Actual bitmap+Levels compositor differs from raw-half/source-parameter oracle");
    const float coverage=l.opacity[2]*(l.mask&&l.mask->enabled?l.mask->background:1);
    require(std::abs(actual.coverage[2]-coverage)<1e-6f,"Levels changed opacity/mask coverage");
    TextureLayer below;below.kind=LayerKind::Fill;below.channels.fill(false);below.channels[2]=true;below.values.roughness=.4f;
    auto blended=isolated;blended.layers.insert(blended.layers.begin(),below);
    const auto mixed=TextureDocument::composite(blended,xy.first,xy.second,tile);
    const float blend=l.blending[2]==LayerBlend::Multiply?.4f*expected:expected;
    require(std::abs(mixed.material.roughness-(.4f*(1-coverage)+blend*coverage))<3e-6f,"Levels/opacity/layer blend order differs from independent formula");
  }
  std::cout<<"PASS bitmap+Levels raw-half/native compositor "<<name<<" samples=12 max_error="<<error<<" isolated_leaf_only=true\n";
}
PreparedSpsm joint_prepare(Builder &b,const s::Archive &image,unsigned mode=0){
  b.levels=128;auto a=b.build(image);auto &p=a.presets.back();const auto curve=rough_curve(p,b);
  triple(p,jf(p,curve,"inputMinimum"),.1f);triple(p,jf(p,curve,"inputMaximum"),.9f);triple(p,jf(p,curve,"gamma"),.8598421812057495f);
  auto stack=jf(p,b.leaf,"actions");auto &items=std::get<s::Array>(p.nodes.at(jf(p,stack,"items")).value).elements;const auto levels=items[1];
  if(mode==1)triple(p,jf(p,curve,"gamma"),0);
  if(mode==2)std::swap(items[0],items[1]);
  if(mode==3)std::get<std::uint64_t>(p.nodes.at(jf(p,levels,"channelTypes")).value)=129;
  if(mode==4)std::get<std::uint64_t>(p.nodes.at(jf(p,curve,"clamp")).value)=2;
  if(mode==5)std::get<s::FloatValues>(p.nodes.at(jf(p,curve,"gamma")).value).values[1]=2;
  if(mode==6){const auto id=p.nodes.size();s::FloatValues value;value.count=1;value.values[0]=.5f;p.nodes.push_back({18,0,1,value});std::get<s::Object>(p.nodes.at(levels).value).fields.push_back({"opacity",id});}
  if(mode==7){const auto id=p.nodes.size();p.nodes.push_back({18,0,1,s::Blob{0,1}});std::get<s::Object>(p.nodes.at(levels).value).fields.push_back({"selection",id});}
  return prepare_spsm_material(sem::inspect_archive(std::move(a)));
}
void joint_synthetic(){
  QTemporaryDir dir;require(dir.isValid(),"Joint private fixture directory");auto archive=image_file((dir.path()+"/joint.h5").toStdString());
  SpsmBitmapTextureCache cache;unsigned reads=0;std::shared_ptr<const SpsmBitmapTexture> image;
  const auto resolver=[&](const auto &d,const auto &p,auto o){++reads;image=cache.load(d,p,o);return image;};
  Builder b;auto original=joint_prepare(b,archive);auto unknown=enhance_prepared_spsm_bitmaps(original,policy(),resolver);
  require(reads==0&&!named(unknown.material,"leaf").visible&&has(unknown,"bitmap-levels-policy-required"),"Unknown Levels decoded/restored a chain");
  auto selected=policy();selected.levels=SpsmLevelsPolicy::NativeScalarLevels;
  auto output=enhance_prepared_spsm_bitmaps(original,selected,resolver);
  require(reads==1&&image&&output.source==original.source&&has(output,"bitmap-native-scalar-levels-policy"),"Joint restoration/provenance missing");
  joint_oracle(output.material,"leaf",*image,.1f,.9f,.8598421812057495f,0,1,false);
  const auto first=fill_channel_texture(named(output.material,"leaf").fill,2);
  auto again=enhance_prepared_spsm_bitmaps(original,selected,resolver);
  require(fill_channel_texture(named(again.material,"leaf").fill,2)==first&&image->texture.pixels==first->pixels,"Repeated resource copied pixels");
  auto reopened=read_material_preset(write_material_preset(output.material));
  joint_oracle(reopened,"leaf",*image,.1f,.9f,.8598421812057495f,0,1,false);
  require(!named(original.material,"leaf").visible&&named(original.material,"leaf").effects.empty(),"Original preparation mutated");
  reject([&]{enhance_prepared_spsm_bitmaps(output,selected,resolver);});
  for(unsigned mode=1;mode<=7;++mode){Builder bad;auto input=joint_prepare(bad,archive,mode);const auto before=reads;auto denied=enhance_prepared_spsm_bitmaps(input,selected,resolver);require(reads==before&&!named(denied.material,"leaf").visible&&named(denied.material,"leaf").effects.empty(),"Invalid late Levels hydrated or partly restored");}
  Builder ordered;ordered.levels=128;auto ordered_archive=ordered.build(archive);auto &p=ordered_archive.presets[0];auto stack=jf(p,ordered.leaf,"actions"),items_id=jf(p,stack,"items");const auto first_action=std::get<s::Array>(p.nodes.at(items_id).value).elements[1];
  auto curve=rough_curve(p,ordered);triple(p,jf(p,curve,"inputMinimum"),.1f);triple(p,jf(p,curve,"inputMaximum"),.9f);triple(p,jf(p,curve,"gamma"),2);triple(p,jf(p,curve,"outputMinimum"),.2f);triple(p,jf(p,curve,"outputMaximum"),.8f);
  std::function<s::NodeId(s::NodeId)> clone=[&](s::NodeId id){auto n=p.nodes.at(id);if(auto o=std::get_if<s::Object>(&n.value))for(auto &f:o->fields)f.value=clone(f.value);if(auto a=std::get_if<s::Array>(&n.value))for(auto &v:a->elements)v=clone(v);auto next=p.nodes.size();p.nodes.push_back(std::move(n));return next;};
  const auto second=clone(first_action),second_curve=jf(p,second,"levels");triple(p,jf(p,second_curve,"inputMinimum"),0);triple(p,jf(p,second_curve,"inputMaximum"),1);triple(p,jf(p,second_curve,"gamma"),.5f);triple(p,jf(p,second_curve,"outputMinimum"),0);triple(p,jf(p,second_curve,"outputMaximum"),1);std::get<s::Array>(p.nodes.at(items_id).value).elements.push_back(second);
  auto ordered_original=prepare_spsm_material(sem::inspect_archive(std::move(ordered_archive)));auto ordered_output=enhance_prepared_spsm_bitmaps(ordered_original,selected,resolver);const auto &chain=named(ordered_output.material,"leaf");require(chain.effects.size()==2&&chain.effects[0].gamma==2&&chain.effects[1].gamma==.5f,"Multiple Levels reordered");auto isolated=ordered_output.material.set;isolated.layers={chain};isolated.layers[0].depth=0;isolated.layers[0].parent=QUuid();isolated.texture_resolution=128;
  for(auto xy:{std::pair(0,0),std::pair(25,81),std::pair(127,127)}){float expected=joint_formula(raw_aa(*image,chain,xy.first,xy.second,1001),.1f,.9f,2,.2f,.8f,false);expected=joint_formula(expected,0,1,.5f,0,1,false);require(std::abs(TextureDocument::composite(isolated,xy.first,xy.second).authored.roughness-expected)<3e-6f,"Two-stage noncommuting Levels source order lost");}
  std::cout<<"PASS joint synthetic Unknown/no-read, strict shared parser, order, nonidentity action opacity, seven atomic denials, original owner, repeat pixels, native Save/Open\n";
}
QByteArray joint_hash(const QString &path){QFile file(path);require(file.open(QIODevice::ReadOnly),"Cannot read original source");QCryptographicHash hash(QCryptographicHash::Sha256);require(hash.addData(&file),"Original digest failed");return hash.result();}
void joint_wood(const QString &dir){
  const auto path=dir+"/used_varnish_wood.spsm";const auto sha=joint_hash(path);auto original=prepare_spsm_material(std::filesystem::path(path.toStdString()));
  SpsmBitmapTextureCache cache;std::map<std::string,std::shared_ptr<const SpsmBitmapTexture>> images;
  const auto resolver=[&](const auto &d,const auto &p,auto o){auto image=cache.load(d,p,o);images[d.resource.dataset.path]=image;return image;};
  auto before=enhance_prepared_spsm_bitmaps(original,policy(),resolver);
  require(images.size()==1&&!named(before.material,"Wood_Darken_base").visible,"Default Levels policy changed real eligibility");
  const auto worn=fill_channel_texture(named(before.material,"Wood_Worn_base").fill,2);
  auto selected=policy();selected.levels=SpsmLevelsPolicy::NativeScalarLevels;auto output=enhance_prepared_spsm_bitmaps(original,selected,resolver);
  const auto &dark=named(output.material,"Wood_Darken_base");
  require(images.size()==2&&dark.visible&&dark.channels[2]&&!dark.channels[0]&&!dark.channels[3]&&dark.effects.size()==1,"Actual independent Wood Darken Roughness not restored");
  require(fill_channel_texture(named(output.material,"Wood_Worn_base").fill,2)==worn,"Worn resource/pixels changed across Levels policies");
  const auto dark_image=images.at("/resources/44a7fb2d1313c31d50caf61e5b2e5550f2297482.image");
  const auto dark_binding=fill_channel_texture(dark.fill,2);
  require(dark_binding&&dark_binding->pixels==dark_image->texture.pixels&&dark_binding->asset!=worn->asset,"Distinct Worn/Darken identities conflated");
  joint_oracle(output.material,"Wood_Darken_base",*dark_image,.29078012704849243f,1,.8598421812057495f,0,1,false);
  const auto &parent=named(output.material,"Wood_Color_Roughness_base");require(parent.mask&&parent.mask->enabled&&parent.mask->background==0&&parent.mask->effects.empty()&&has(output,"bitmap-zero-ancestor-mask"),"Known zero ancestor mask was unmasked/lost");
  for(auto xy:{std::pair(0,0),std::pair(63,64),std::pair(127,127)}){auto old=original.material.set,newset=output.material.set;old.texture_resolution=newset.texture_resolution=128;const auto a=TextureDocument::composite(old,xy.first,xy.second),b=TextureDocument::composite(newset,xy.first,xy.second);require(a.material.roughness==b.material.roughness&&a.coverage[2]==b.coverage[2],"Restoration bypassed original ancestor coverage");}
  auto saved=write_material_preset(output.material);auto reopened=read_material_preset(saved);saved.clear();
  joint_oracle(reopened,"Wood_Darken_base",*dark_image,.29078012704849243f,1,.8598421812057495f,0,1,false);
  const auto &saved_parent=named(reopened,"Wood_Color_Roughness_base");require(saved_parent.mask&&saved_parent.mask->enabled&&saved_parent.mask->background==0,"Save/Open unmasked ancestor");
  auto repeated=enhance_prepared_spsm_bitmaps(original,selected,resolver);require(fill_channel_texture(named(repeated.material,"Wood_Darken_base").fill,2)==dark_binding&&images.size()==2,"Repeated joint resource copied/decoded another owner");
  require(sha==joint_hash(path)&&output.source==original.source&&!output.appearance_verified,"Original source changed or appearance claim made");
  std::cout<<"PASS REAL Wood_Darken_base restored under explicit policy; two distinct datasets, shared per-resource owners, Worn unchanged, native Save/Open and zero ancestor mask retained; original_sha="<<sha.toHex().toStdString()<<" appearance_verified=false\n";
}
} // namespace
int main(int argc,char **argv){QCoreApplication app(argc,argv);std::cout<<std::unitbuf;try{if(argc>1&&std::string_view(argv[1])=="--runtime-baseline")return 0;synthetic();joint_synthetic();if(argc>1){fixtures(argv[1]);joint_wood(QString::fromUtf8(argv[1]));}return 0;}catch(const std::exception &e){std::cerr<<"Joint bitmap Levels failed: "<<e.what()<<'\n';return 1;}}
