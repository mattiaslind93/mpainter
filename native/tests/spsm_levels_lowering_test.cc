#include "SpsmLevelsLowering.hh"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <iostream>
#include <limits>
using namespace bts;
namespace s = bts::spsm;
namespace sem = s::semantic;
namespace {
void require(bool yes, const char *why) { if (!yes) throw std::runtime_error(why); }
void near(float x, float y, const char *why) { if (std::abs(x-y) > 2e-6f) {
  std::cerr << why << " actual=" << x << " expected=" << y << '\n'; throw std::runtime_error(why); } }
template<class F> void reject(F f) { bool rejected=false;try { f(); } catch(const std::exception &) {rejected=true;}require(rejected,"Unsafe input accepted"); }
bool has(const PreparedSpsm &p, std::string_view code) { return std::any_of(p.diagnostics.begin(),p.diagnostics.end(),[&](auto &d){return d.code==code;}); }
const TextureLayer &leaf(const PreparedSpsm &p) { for (auto &l:p.material.set.layers) if(l.name=="leaf")return l;throw std::runtime_error("Missing leaf"); }
struct Builder {
  s::Preset p;
  s::NodeId source,fill,layers[2],curves[2],leaf_id,root;
  template<class T> s::NodeId add(T v) { auto id=p.nodes.size();p.nodes.push_back({18,0,1,std::move(v)});return id; }
  s::NodeId integer(std::uint64_t x){return add(x);}
  s::NodeId flag(bool x){return add(x);}
  s::NodeId scalar(float x){s::FloatValues v;v.count=1;v.values[0]=x;return add(v);}
  s::NodeId rgb(float x){s::FloatValues v;v.count=3;for(int c=0;c<3;++c)v.values[c]=x;return add(v);}
  s::NodeId object(std::string name,std::initializer_list<s::Field> fields){return add(s::Object{std::move(name),fields});}
  s::NodeId array(std::vector<s::NodeId> v){return add(s::Array{std::move(v)});}
  s::NodeId stack(std::string name,std::vector<s::NodeId> v){return object(std::move(name),{{"items",array(std::move(v))}});}
  s::NodeId blend(float opacity){return object("DataBlending",{{"channelTypes",integer(128)},{"blendingMode",integer(2)},{"opacity",scalar(opacity)}});}
  void set(s::NodeId id,std::string_view name,s::NodeId value){auto &o=std::get<s::Object>(p.nodes.at(id).value);for(auto &f:o.fields)if(f.name==name){f.value=value;return;}o.fields.push_back({std::string(name),value});}
  s::NodeId get(s::NodeId id,std::string_view name){for(auto &f:std::get<s::Object>(p.nodes.at(id).value).fields)if(f.name==name)return f.value;throw std::runtime_error("Missing test field");}
  void action_list(std::vector<s::NodeId> actions){set(leaf_id,"actions",stack("DataStackActions",std::move(actions)));}
  Builder(){
    source=object("DataSourceUniform",{{"channelTypes",integer(128)},{"color",rgb(.4f)},{"opacity",scalar(.7f)}});
    fill=object("DataActionFill",{{"enabled",flag(true)},{"channelTypes",integer(UINT64_MAX)},{"sources",array({source})},{"perChannelBlending",array({blend(.8f)})},{"tags",integer(0)}});
    for(int i=0;i<2;++i){
      curves[i]=object("DataLevels",{{"inputMinimum",rgb(i?0:.1f)},{"inputMaximum",rgb(i?1:.9f)},{"gamma",rgb(i?.5f:2)},{"outputMinimum",rgb(i?0:.2f)},{"outputMaximum",rgb(i?1:.8f)},{"clamp",integer(1)}});
      layers[i]=object("DataActionFilterLevels",{{"channelTypes",integer(128)},{"enabled",flag(true)},{"levels",curves[i]},{"selection",add(s::Blob{})},{"tags",integer(0)}});
    }
    leaf_id=object("DataLayerColor",{{"label",add(std::string("leaf"))},{"enabled",flag(true)},{"actions",stack("DataStackActions",{fill,layers[0],layers[1]})},{"maskInitial",scalar(.4f)},{"perChannelBlending",array({blend(.6f)})}});
    root=object("DataLayerGroup",{{"label",add(std::string("root"))},{"enabled",flag(true)},{"subStack",stack("DataStackLayers",{leaf_id})},{"maskInitial",scalar(1)}});
    p.root=object("DataLayersPreset",{{"layerGroup",root}});p.dataset="/preset.bin";p.encoded={1,2,3,4};
  }
  PreparedSpsm prepare(){
    // Materialize the reachable synthetic tree after mutations, exactly like a
    // structural parse: no detached replaced nodes or artificial shared edges.
    std::vector<s::Node> nodes;
    std::function<s::NodeId(s::NodeId)> clone=[&](s::NodeId id){
      auto n=p.nodes.at(id);
      if(auto o=std::get_if<s::Object>(&n.value))for(auto &f:o->fields)f.value=clone(f.value);
      if(auto a=std::get_if<s::Array>(&n.value))for(auto &v:a->elements)v=clone(v);
      auto next=nodes.size();nodes.push_back(std::move(n));return next;
    };
    p.root=clone(p.root);p.nodes=std::move(nodes);
    s::Archive a;a.presets.push_back(std::move(p));return prepare_spsm_material(sem::inspect_archive(std::move(a)));
  }
};
PreparedSpsm lower(const PreparedSpsm &p){return prepare_spsm_levels(p,{SpsmLevelsPolicy::NativeScalarLevels});}
struct ExpectedCurve {float low,high,gamma,out_low,out_high;bool clamp;};
using ExpectedCurves=std::array<ExpectedCurve,2>;
ExpectedCurves expected_curves(){return {{{.1f,.9f,2,.2f,.8f,true},{0,1,.5f,0,1,true}}};}
// Independent source-parameter oracle, not native levels_curve_value or the
// lowered native fields (a swapped source field must fail this test too).
float formula(float value,const ExpectedCurve &e){double t=(double(value)-e.low)/(double(e.high)-e.low);if(t>0)t=std::pow(t,1/double(e.gamma));t=double(e.out_low)+(double(e.out_high)-e.out_low)*t;t=std::clamp(t,-65504.,65504.);if(e.clamp)t=std::clamp(t,0.,1.);return float(t);}
void oracle(const PreparedSpsm &p,ExpectedCurves curves=expected_curves()){
  const auto &l=leaf(p);require(l.visible&&l.channels[2]&&l.effects.size()==2,"Roughness chain not restored");
  near(l.values.roughness,.4f,"Source initial Roughness not mapped");
  float expected=.4f;for(unsigned i=0;i<2;++i){const auto &e=l.effects[i];const auto &src=curves[i];near(e.input_low,src.low,"inputMinimum mapping");near(e.input_high,src.high,"inputMaximum mapping");near(e.gamma,src.gamma,"gamma mapping");near(e.output_low,src.out_low,"outputMinimum mapping");near(e.output_high,src.out_high,"outputMaximum mapping");require(e.clamp==src.clamp,"clamp mapping");expected=formula(expected,src);}
  const auto actual=TextureDocument::composite(p.material.set);
  near(actual.authored.roughness,expected,"Actual compositor authored Roughness differs from independent formula");
  near(actual.coverage[2],.6f*.8f*.7f*.4f,"Coverage/initial action/layer opacity order differs");
  for(int c:{0,1,3,4})require(!l.channels[c],"Unsupported sibling channel was activated");
  require(!l.ao_enabled,"Unsupported AO was activated");
}
void tests(){
  Builder b;auto original=b.prepare();require(!leaf(original).visible&&leaf(original).effects.empty(),"Base preparer unexpectedly evaluates Levels");
  auto unknown=prepare_spsm_levels(original);require(!leaf(unknown).visible&&has(unknown,"levels-policy-required"),"Unknown default policy restored a chain");
  auto output=lower(original);oracle(output);
  require(output.source==original.source&&output.source->source().presets[0].encoded==s::ImageBytes({1,2,3,4}),"Source provenance not retained");
  require(output.partial&&!output.appearance_verified&&has(output,"native-scalar-levels-policy")&&has(output,"levels-roughness-restored"),"Policy/partial diagnostics missing");
  require(!leaf(original).visible&&leaf(original).effects.empty(),"Input preparation mutated");
  for(auto &d:output.diagnostics)if(d.code=="native-scalar-levels-policy")require(!output.diagnostic_path(d).empty(),"Missing source path");
  unsigned bound=0;for(auto &a:output.actions)if(a.lowered)++bound;require(bound==3,"Fill/Levels action associations not updated");
  reject([&]{prepare_spsm_levels(original,{SpsmLevelsPolicy::NativeScalarLevels,1});});
  auto modified=original;modified.material.set.layers[0].effects.push_back(LevelsEffect{});reject([&]{lower(modified);});
  auto hierarchy=original;hierarchy.material.set.layers[0].parent=QUuid::createUuid();reject([&]{lower(hierarchy);});
  auto absent=original;absent.source.reset();reject([&]{lower(absent);});
  const std::vector<std::function<void(Builder&)>> unsafe{
    [](auto &b){b.set(b.curves[1],"gamma",b.rgb(0));},
    [](auto &b){b.set(b.curves[1],"gamma",b.rgb(std::numeric_limits<float>::quiet_NaN()));},
    [](auto &b){auto id=b.rgb(2);std::get<s::FloatValues>(b.p.nodes[id].value).values[1]=3;b.set(b.curves[1],"gamma",id);},
    [](auto &b){b.set(b.curves[1],"inputMaximum",b.rgb(0));},
    [](auto &b){b.set(b.curves[1],"clamp",b.integer(2));},
    [](auto &b){b.set(b.curves[1],"futureFormula",b.scalar(1));},
    [](auto &b){b.set(b.layers[1],"opacity",b.scalar(.5f));},
    [](auto &b){b.set(b.layers[1],"tags",b.integer(1));},
    [](auto &b){s::Blob blob;blob.size=1;b.set(b.layers[1],"selection",b.add(blob));},
    [](auto &b){b.set(b.layers[1],"channelTypes",b.integer(129));},
    [](auto &b){b.action_list({b.layers[0],b.fill,b.layers[1]});},
    [](auto &b){b.action_list({b.fill,b.layers[0],b.layers[1],b.object("DataActionPaint",{{"enabled",b.flag(true)},{"channelTypes",b.integer(128)}})});},
    [](auto &b){b.action_list({b.fill,b.layers[0],b.layers[1],b.fill});},
    [](auto &b){b.set(b.fill,"depthCulling",b.flag(true));},
    [](auto &b){std::get<s::Object>(b.p.nodes[b.source].value).class_name="DataSourceUnknown";},
    [](auto &b){b.set(b.leaf_id,"maskActions",b.stack("DataStackActions",{b.object("DataActionGroup",{{"enabled",b.flag(true)}})}));},
    [](auto &b){b.set(b.root,"maskActions",b.stack("DataStackActions",{b.object("DataActionGroup",{{"enabled",b.flag(true)}})}));},
    [](auto &b){b.set(b.root,"enabled",b.flag(false));},
    [](auto &b){b.set(b.leaf_id,"enabled",b.flag(false));},
    [](auto &b){b.set(b.leaf_id,"enabledUVTileDefault",b.flag(false));},
    [](auto &b){b.set(b.leaf_id,"perChannelBlending",b.array({b.object("DataBlending",{{"channelTypes",b.integer(128)},{"blendingMode",b.integer(77)},{"opacity",b.scalar(1)}})}));}
  };
  for(auto &change:unsafe){Builder bad;change(bad);auto input=bad.prepare();auto rejected=lower(input);require(!leaf(rejected).visible&&leaf(rejected).effects.empty(),"Unsupported late dependency partially restored Roughness");require(!has(rejected,"levels-roughness-restored"),"Unsupported chain reported restored");}
  // All five curve fields are operative; each changed input is evaluated by
  // the actual native compositor, never folded into an untracked constant.
  for(auto name:{"inputMinimum","inputMaximum","gamma","outputMinimum","outputMaximum"}){
    Builder change;float value=std::string_view(name)=="inputMaximum"?.95f:std::string_view(name)=="gamma"?3.f:.15f;change.set(change.curves[0],name,change.rgb(value));auto curves=expected_curves();if(std::string_view(name)=="inputMinimum")curves[0].low=value;else if(std::string_view(name)=="inputMaximum")curves[0].high=value;else if(std::string_view(name)=="gamma")curves[0].gamma=value;else if(std::string_view(name)=="outputMinimum")curves[0].out_low=value;else curves[0].out_high=value;auto input=change.prepare();auto out=lower(input);oracle(out,curves);require(leaf(out).effects[0].id!=leaf(output).effects[0].id,"Effect identities reused across independent preparations");
  }
  Builder disjoint;auto color=disjoint.object("DataActionFilterLevels",{{"channelTypes",disjoint.integer(1)},{"enabled",disjoint.flag(true)},{"levels",disjoint.curves[0]}});disjoint.action_list({disjoint.fill,color,disjoint.layers[0],disjoint.layers[1]});auto a=disjoint.prepare();oracle(lower(a));
  Builder inactive;auto hidden=inactive.object("DataActionPaint",{{"enabled",inactive.flag(false)}});inactive.action_list({inactive.fill,hidden,inactive.layers[0],inactive.layers[1]});auto h=inactive.prepare();oracle(lower(h));
  // Clamp policy affects both signs; the source enum is never cast to native.
  for(unsigned clamp:{0u,1u}){Builder c;c.set(c.curves[0],"inputMinimum",c.rgb(.8f));c.set(c.curves[0],"clamp",c.integer(clamp));auto curves=expected_curves();curves[0].low=.8f;curves[0].clamp=clamp==1;auto in=c.prepare();auto out=lower(in);oracle(out,curves);}
  std::cout<<"PASS synthetic independent Roughness Levels: native compositor/formula, source ownership, order, all curve fields, opacity/masks, 21 unsupported dependencies, Unknown, low RAM\n";
}
QByteArray hash(const QString &path){QFile f(path);require(f.open(QIODevice::ReadOnly),"Cannot hash fixture");QCryptographicHash h(QCryptographicHash::Sha256);require(h.addData(&f),"Cannot digest fixture");return h.result();}
void fixtures(const QString &dir){
  for(auto name:{"dirty-plastic-robot.spsm","shd-hard-rust.spsm","sratched-metal-pealing-paint-dirt.spsm","used_varnish_wood.spsm","worn leather chest.spsm"}){
    const auto path=dir+"/"+name;auto digest=hash(path);auto original=prepare_spsm_material(std::filesystem::path(path.toStdString()));auto out=lower(original);require(out.source==original.source&&hash(path)==digest,"Real fixture source changed");
    std::size_t restored=0;for(auto &d:out.diagnostics)if(d.code=="levels-roughness-restored")++restored;
    for(auto &l:out.material.set.layers)if(!l.effects.empty())require(l.channels[2]&&l.effects.size()&&l.kind==LayerKind::Fill,"Unexpected real material effect target");
    std::cout<<"PASS unchanged real source "<<name<<" streams="<<out.source->source().presets.size()<<" independent Roughness chains="<<restored<<" appearance_verified=false\n";
  }
}
} // namespace
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{if(argc>1&&std::string_view(argv[1])=="--runtime-baseline")return 0;tests();if(argc>1)fixtures(QString::fromLocal8Bit(argv[1]));return 0;}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
