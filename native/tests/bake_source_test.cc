#include "BakeSourceControls.hh"
#include <QApplication>
#include <QDir>
#include <QJsonDocument>
#include <QFile>
#include <cassert>
int main(int argc,char **argv){
 bts::initialize_numeric_locale();QApplication app(argc,argv);assert(!bts::bake_source_options({}).frame&&bts::bake_source_options({}).scale==1);
 bts::BakeSourceOptions source;source.frame=24.5;source.scale=.01;QJsonObject recipe;store_bake_source_options(recipe,source);assert(bts::bake_source_options(recipe)==source);
 for(const auto &invalid:{QJsonObject{{"source_scale",0}},QJsonObject{{"source_scale",-1}},QJsonObject{{"source_scale","bad"}},QJsonObject{{"source_frame_enabled",true},{"source_frame",1e9}},QJsonObject{{"source_frame_enabled",1}}}){bool rejected=false;try{bts::bake_source_options(invalid);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);}
 bts::SceneSnapshot scene;scene.has_bounds=true;scene.minimum={100,200,300};scene.maximum={200,300,400};bts::UvFaceTriangle triangle;triangle.positions[0]={100,200,300};triangle.corners[0]={1.25,.25};scene.uv_triangles.push_back(triangle);const auto uv=scene.uv_triangles.front().corners;bts::scale_bake_source(scene,.01);assert(scene.minimum[0]==1&&scene.maximum[2]==4&&scene.uv_triangles[0].positions[0][2]==3&&scene.uv_triangles[0].corners==uv);
 bts::BakeSourceControls controls(nullptr,"test",recipe);int changes=0;controls.changed=[&]{++changes;};assert(controls.options()==source);controls.findChild<QCheckBox *>("testSourceFrameEnabled")->setChecked(false);assert(!controls.options().frame&&changes==1);controls.synchronize(recipe);assert(controls.options()==source&&changes==1);assert(controls.findChild<QDoubleSpinBox *>("testSourceScale")->locale().decimalPoint()==".");
 const QString filter=bts::bake_mesh_filter;for(const auto *extension:{"*.obj","*.abc","*.usd","*.usda","*.usdc","*.usdz"})assert(filter.contains(extension));
 if(argc>1){QDir().mkpath(argv[1]);controls.resize(430,170);controls.show();app.processEvents();controls.grab().save(QString(argv[1])+"/source-controls.png");}
 puts("Bake sources: formats, frame/units, legacy defaults, validation, live UI and JSON PASS");
}
