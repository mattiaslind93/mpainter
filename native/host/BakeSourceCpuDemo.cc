#include "BakeSourceCpuDemo.hh"
#include "BlenderHost.hh"
#include "AutoBakeEngine.hh"
#include "BakeMatch.hh"
#include "SurfaceBakeProof.hh"
#include "RebakeProof.hh"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
namespace bts {
int run_bake_source_cpu_demo(BlenderHost &host,const QString &directory){
 if(qEnvironmentVariableIsSet("BTS_REBAKE_REGRESSION"))return rebake_proof(directory);
 if(qEnvironmentVariableIsSet("BTS_SURFACE_FIXTURES"))return surface_bake_proof(host,directory);
 const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
 const auto path=[&](const QString &name){return QDir(directory).filePath("fixtures/"+name).toStdString();};
 const auto before=host.scene_snapshot();auto low=std::make_shared<const SceneSnapshot>(host.load_bake_source(path("low.obj")));require(low->mesh_objects==2,"Low source lost objects");
 AutoBakeOptions settings;settings.maps.resolution=settings.maps.height=64;settings.maps.dilation=0;settings.maps.rays=16;settings.maps.enabled.fill(false);settings.maps.enabled[0]=settings.maps.enabled[3]=settings.maps.enabled[4]=true;settings.ray.frontal=settings.ray.rear=.5;settings.ray.relative=false;settings.ray.match_names=true;
 const auto uid=low->materials.front().session_id;std::atomic<bool> cancel{false};SkewField field;
 const auto bake=[&](const std::string &file,const BakeSourceOptions &options,std::shared_ptr<const SceneSnapshot> cage={}){
  auto high=std::make_shared<const SceneSnapshot>(host.load_bake_source(file,options));require(high->mesh_objects==2,"High source lost objects");
  for(const auto &[id,name]:low->object_names){bool matched=false;for(const auto &[other,high_name]:high->object_names)matched|=bake_names_match(name,high_name,"_low","_high");require(matched,"Match by mesh name suffixes changed on import");}
  AutoBakeEngine engine(low,high,cage);auto options_for_bake=settings;options_for_bake.ray.use_cage=bool(cage);return engine.bake(uid,"BakeSurface",{}, {1001,1002},field,options_for_bake,cancel);
 };
 const auto reference=bake(path("high.obj"),{});QJsonArray cases;
 const auto compare=[&](const MeshMaps &maps,const MeshMaps &expected,const QString &source){double maximum=0,squared=0;std::uint64_t count=0;QJsonArray tiles;
  for(int role:{0,3,4}){require(maps[role]&&expected[role],"Bake omitted a required map");for(int tile:{1001,1002}){const auto &p=maps[role]->tiles.at(tile).pixels->rgba,&q=expected[role]->tiles.at(tile).pixels->rgba;require(p.size()==q.size(),"Bake dimensions differ");bool occupied=false;for(std::size_t i=0;i<p.size();++i){const double delta=std::abs(p[i]-q[i]);maximum=std::max(maximum,delta);squared+=delta*delta;++count;if(i%4==3&&p[i]>0)occupied=true;}require(occupied,"Bake produced an empty UDIM tile");tiles.append(QJsonObject{{"role",role},{"tile",tile},{"hash",QString::fromStdString(maps[role]->tiles.at(tile).hash)}});}}
  require(maximum<.025&&squared/std::max<std::uint64_t>(1,count)<1e-5,"ABC/USD bake differs from OBJ reference");cases.append(QJsonObject{{"source",source},{"maximum_error",maximum},{"mse",squared/std::max<std::uint64_t>(1,count)},{"tiles",tiles}});
 };
 for(const QString name:{"high.abc","high.usdc","high-cm.usdc"})compare(bake(path(name),{}),reference,name);
 BakeSourceOptions centimeters;centimeters.scale=.01;compare(bake(path("high-cm.abc"),centimeters),reference,"centimetre Alembic scale 0.01");
 const auto cage_obj=std::make_shared<const SceneSnapshot>(host.load_bake_source(path("cage.obj")));const auto cage_abc=std::make_shared<const SceneSnapshot>(host.load_bake_source(path("cage.abc")));TexturePadding::validate_cage(*low,*cage_abc);compare(bake(path("high.abc"),{},cage_abc),bake(path("high.obj"),{},cage_obj),"Alembic custom cage");
 BakeSourceOptions second;second.frame=2;QJsonArray animation;
 for(const QString name:{"animated.abc","animated.usdc"}){auto first=host.load_bake_source(path(name));auto next=host.load_bake_source(path(name),second);const double shift=next.maximum[2]-first.maximum[2];require(std::abs(shift-.2)<1e-5,"Animated bake source ignored selected frame");animation.append(QJsonObject{{"source",name},{"z_shift",shift}});}
 for(const QString name:{"low.abc","low.usdc"}){auto project=host.load_bake_source(path(name));std::set<int> tiles;for(const auto &triangle:project.uv_triangles){double u=0,v=0;for(const auto &p:triangle.corners){u+=p[0]/3.;v+=p[1]/3.;}tiles.insert(1001+int(std::floor(u))+10*int(std::floor(v)));}require(tiles==std::set<int>{1001,1002},"ABC/USD low source lost UDIM coordinates");}
 const auto after=host.scene_snapshot();require(before.object_names==after.object_names&&before.uv_triangles.size()==after.uv_triangles.size(),"Bake source changed live scene");
 QFile file(QDir(directory).filePath("bake-sources.json"));require(file.open(QIODevice::WriteOnly),"Cannot write bake source proof");file.write(QJsonDocument(QJsonObject{{"native_blender_import",true},{"maps",QJsonArray{"Normal","AO","Curvature"}},{"tiles",QJsonArray{1001,1002}},{"comparisons",cases},{"animation",animation},{"live_scene_unchanged",true}}).toJson());return 0;
}
}
