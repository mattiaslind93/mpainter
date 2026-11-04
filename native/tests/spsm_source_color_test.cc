#pragma push_macro("main")
#undef main
#define main spsm_source_bitmap_previous_main
#include "spsm_source_bitmap_test.cc"
#pragma pop_macro("main")
#include "IMB_colormanagement.hh"
namespace {
const TextureLayer &color_leaf(const StudioAsset &asset,const char *name){for(const auto &layer:asset.material_preset->set.layers)if(layer.name==name)return layer;throw std::runtime_error("Missing Color Source leaf");}
QStringList color_tags(const QString &choice,bool roughness,bool scalar){
 auto tags=bitmap_tags();tags[0]=QString("spsm:policy:bitmap-roughness=")+(roughness?"raw-r-ignore-a":"unknown");tags.append("spsm:policy:bitmap-color="+choice);if(scalar)tags.append("spsm:policy:levels=native-scalar");return tags;
}
void color_source(const QString &fixtures){
 QTemporaryDir cache;require(cache.isValid(),"Color Source cache failed");qputenv("BTS_RESOURCE_CACHE",cache.path().toUtf8());configure_spsm_source_hydration();
 auto original=asset(fixtures+"/used_varnish_wood.spsm");const auto sha=original.hash();const auto role=std::string(blender::IMB_colormanagement_working_space_get());
 const auto prepare=[&](const QString &choice,bool roughness,bool levels){auto a=original;a.search_terms=color_tags(choice,roughness,levels);require(!hydrate_spsm_asset(a)&&!a.material_preset,"GUI cold Color Source performed work");return std::async(std::launch::async,worker,a).get();};
 const auto unknown=prepare("unknown",false,true);for(const auto &l:unknown.material_preset->set.layers)require(!fill_has_texture(l.fill),"Unknown Color/Roughness performed bitmap hydration");
 const auto color_only=prepare("native-linear-rec709-rgb-levels-ignore-a",false,false);
 const auto combined=prepare("native-linear-rec709-rgb-levels-ignore-a",true,true);
 const auto aces=prepare("native-acescg-rgb-levels-ignore-a",true,true);
 const auto deferred=prepare("native-linear-rec709-rgb-levels-ignore-a",true,false);
 const auto prepared=prepared_spsm_source(combined);require(prepared&&prepared->policy.bitmap_color==SpsmBitmapColorInput::NativeLinearRec709RGBLevelsIgnoreA&&prepared->policy.roughness_raw_r_ignore_a,"Worker lost chosen independent policies");
 for(const char *name:{"Wood_Worn_base","Wood_Darken_base"}){
  const auto &c=color_leaf(color_only,name),&r=color_leaf(combined,name),&a=color_leaf(aces,name);
  require(c.visible&&c.channels[0]&&!c.channels[2]&&c.fill.texture&&!fill_channel_texture(c.fill,2),"Color-only restored Roughness or lost Color");
  require(r.visible&&r.channels[0]&&r.channels[2]&&r.fill.texture==c.fill.texture&&r.fill.texture==a.fill.texture&&fill_channel_texture(r.fill,2)==fill_channel_texture(a.fill,2),"Source policy variants copied or changed raw bitmap bindings");
  require(r.fill.texture!=fill_channel_texture(r.fill,2),"Distinct actual Color/Roughness sources were aliased");
  require(raw_linear_color_source(c)&&raw_linear_color_source(r)&&raw_linear_color_source(a)&&r.effects.back().linear_color->input==LinearColorInput::Rec709&&a.effects.back().linear_color->input==LinearColorInput::ACEScg,"Named interpretation boundary/policy was lost");
  require(effects_json(r.effects)!=effects_json(a.effects)&&r.fill.texture->pixels==a.fill.texture->pixels&&r.fill.texture->hash==a.fill.texture->hash,"Color policy contaminated raw resource identity");
 }
 const auto &dark=color_leaf(deferred,"Wood_Darken_base"),&worn=color_leaf(deferred,"Wood_Worn_base");
 require(!dark.visible&&!fill_has_texture(dark.fill)&&dark.effects.empty()&&worn.visible&&worn.channels[0]&&worn.channels[2],"Missing selected scalar Levels failed whole-leaf atomicity");
 const auto &parent=color_leaf(combined,"Wood_Color_Roughness_base");require(parent.mask&&parent.mask->enabled&&parent.mask->background==0&&parent.mask->effects.empty(),"Color Source unmasked ancestor0");
 for(const auto *a:{&unknown,&color_only,&combined,&aces,&deferred}){
  const auto source=prepared_spsm_source(*a);require(source&&source->original_bytes==prepared->original_bytes&&source->cache_path==prepared->cache_path&&a->hash()==sha&&source->prepared.partial&&!source->prepared.appearance_verified,"Source variants changed original owner/path/SHA or completeness claim");
 }
 require(role==blender::IMB_colormanagement_working_space_get(),"Worker named Color policy changed global working space");
 require(prepared->report.contains("CHOSEN native interpretation")&&prepared->report.contains("same dataset")&&prepared->report.contains("distinct source records"),"Report overclaimed color-space evidence or unconditional sharing");
 require(file_hash(fixtures+"/used_varnish_wood.spsm")==sha,"Original private source changed");
 std::cout<<"PASS Color Source worker: defaultUnknown no hydration, Rec709/ACEScg explicit choices, Color-only/combined, whole-Darken defer without scalar policy, shared originals/same-dataset raw bindings across choices, distinct actual records, unchanged global role/ancestor0/SHA; appearance unverified\n";
}
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);std::cout<<std::unitbuf;try{require(argc==2,"Expected fixture directory");CLG_init();blender::IMB_init();policy_parse();color_source(QString::fromUtf8(argv[1]));blender::IMB_exit();CLG_exit();return 0;}catch(const std::exception &error){std::cerr<<"Color Source worker FAIL "<<error.what()<<'\n';return 1;}}
