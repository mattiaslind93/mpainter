#include "WarpIO.hh"
#include "WarpProjection.hh"
#include <cassert>
#include <iostream>
using namespace bts;
int main(){
 FillProjection p;p.mode=5;auto author=std::make_shared<WarpGrid>();author->at(1,1).position[0]+=.1;author->align_tangents();p.warp=author;p=toggle_warp_geometry(p,true);assert(warp_geometry_enabled(p));
 auto cache=std::make_shared<WarpGeometryCache>();cache->authored=p.warp;cache->offset=p.offset3;cache->rotation=p.rotation3;cache->scale=p.scale3;cache->fitted.grid=author;p.warp_fit=cache;
 const auto enabled=warp_settings_json(p);FillProjection restored;read_warp_settings(restored,enabled);assert(restored.warp_geometry->enabled&&!restored.warp_fit&&warp_settings_json(restored)==enabled);
 auto frozen=std::make_shared<WarpGrid>(*author);frozen->at(1,1).position[1]+=.2;frozen->align_tangents();cache->fitted.grid=frozen;p=toggle_warp_geometry(p,false);assert(!warp_geometry_enabled(p)&&p.warp==frozen);const auto off=warp_settings_json(p);read_warp_settings(restored,off);assert(!restored.warp_geometry->enabled&&warp_settings_json(restored)==off);restored=toggle_warp_geometry(restored,true);assert(std::abs(restored.warp->at(1,1).position[1]-author->at(1,1).position[1])<1e-9);
 auto legacy=enabled;legacy.remove("geometry");read_warp_settings(restored,legacy);assert(!restored.warp_geometry);auto invalid=enabled;auto settings=invalid["geometry"].toObject();settings["authored_reference"]=warp_grid_json(author);settings["frozen_reference"]=warp_grid_json(frozen);invalid["geometry"]=settings;bool rejected=false;try{read_warp_settings(restored,invalid);}catch(const std::invalid_argument &){rejected=true;}assert(rejected&&!restored.warp_geometry);
 p=FillProjection{};p.mode=5;p.rotation3={20,30,40};p.scale3={.2,.3,.4};p.offset3={.1,-.2,.3};const WarpVector center{1,2,3},local{.2,-.4,.1};const double extent=2;const auto frame=warp_projection_frame(p,center,extent);FillSurfaceAtlas atlas;atlas.center=center;atlas.extent=extent;FillSurfacePoint surface;surface.valid=true;const auto world=frame.world(local);for(int c=0;c<3;++c)surface.position[c]=float(world[c]);const auto inverse=fill_planar_frame(p,surface,atlas);for(int c=0;c<3;++c)assert(std::abs(inverse.position[c]-local[c])<1e-6);
 // Small signed fit errors must not punch holes into the projected decal;
 // freezing the fit preserves coverage, while legacy projectors remain one-sided.
 FillProjection coverage;coverage.mode=5;PlanarFrame sample;WarpCoordinate coord;coord.u=coord.v=.5;coord.depth=.002;
 assert(fill_warp_coverage(coverage,sample,coord)==0);coverage=toggle_warp_geometry(coverage,true);
 assert(fill_warp_coverage(coverage,sample,coord)==1);auto visible=std::make_shared<WarpGrid>();auto fit=std::make_shared<WarpGeometryCache>();fit->authored=warp_authored_grid(coverage);fit->scale=coverage.scale3;fit->fitted.grid=visible;coverage.warp_fit=fit;coverage=toggle_warp_geometry(coverage,false);
 assert(fill_warp_coverage(coverage,sample,coord)==1);coord.depth=coverage.warp_depth+.01;assert(fill_warp_coverage(coverage,sample,coord)==0);
 std::cout<<"Warp geometry on/off references and legacy/invalid codec, runtime exclusion and native projector-frame inverse PASS\n";
}
