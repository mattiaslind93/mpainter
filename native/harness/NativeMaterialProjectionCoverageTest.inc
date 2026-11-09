// SPDX-License-Identifier: GPL-3.0-or-later
{
 auto *tree=make_tree("BTS_Coord_UV");NativeMaterialGpuProgram program(*tree,0,0);std::uint64_t checks=0;
 // Longitude seams, near-poles and the explicitly defined zero-radius center.
 for(int mode:{3,4}){FillProjection p;p.mode=mode;p.filtering=1;p.depth_culling=false;context.projection=native_material_projection(p,&atlas,set);
  for(int i=0;i<width*height;++i){auto &point=points[i];point.position=i%5==0?std::array<float,3>{-.00001f,.4f,.02f}:i%5==1?std::array<float,3>{.00001f,.4f,.02f}:i%5==2?std::array<float,3>{.04f,0,.6f}:i%5==3?std::array<float,3>{.04f,0,-.6f}:std::array<float,3>{0,0,0};point.normal={.6f,0,.8f};point.valid=true;point.footprint=0;}upload_projection_points();
  for(int tile:{1001,1002,1011}){context.tile=tile;program.dispatch(target.get(),{},context,{0,0,width,height});auto *actual=pixels();require(actual,"Seam/polar projection readback missing");const auto release=qScopeGuard([&]{MEM_delete(actual);});for(int i=0;i<width*height;++i){const auto &point=points[i];const auto expected=projection_oracle::coordinate(projection_oracle::samples(p,{point.position[0],point.position[1],point.position[2]},{.6,0,.8},{},true,tile,2,{1,1},1));for(int c=0;c<4;++c){require(std::isfinite(actual[i*4+c])&&std::abs(actual[i*4+c]-expected[c])<2e-6,"Longitude seam/near-pole/zero-radius coordinate mismatch");++checks;}}}
 }
 for(int test=0;test<6;++test){FillProjection p;p.filtering=1;p.mode=test<3?2:test==3?3:test==4?4:5;p.depth_culling=false;std::array<float,3> position{0,1.5,0},normal{0,-1,0};double coverage=.5;
  if(test==0){p.depth_culling=true;p.depth_hardness=.5;}
  if(test==1){p.depth_culling=true;p.depth_hardness=.5;position={0,3,0};coverage=0;}
  if(test==2){position={0,0,0};normal={0,1,0};p.backface_culling=true;coverage=0;}
  if(test==3){position={1.4,0,0};p.shape_crop=true;coverage=0;}
  if(test==4){position={.4,0,0};normal={-1,0,0};p.backface_culling=true;coverage=0;}
  if(test==5){position={0,2,0};coverage=0;}
  for(auto &point:points){point.position=position;point.normal=normal;point.valid=true;point.footprint=0;}upload_projection_points();context.projection=native_material_projection(p,&atlas,set);program.dispatch(target.get(),{},context,{0,0,width,height});auto *actual=pixels();require(actual,"Native projection coverage readback missing");const auto release=qScopeGuard([&]{MEM_delete(actual);});for(int i=0;i<width*height;++i)for(int c=0;c<4;++c){const double expected=coverage==0?0:c==3?coverage:c<2?.5:0;require(std::abs(actual[i*4+c]-expected)<2e-6,"Native depth/crop/backface/Warp reach coverage differs from independent stencil");++checks;}
 }
 coverage_report={{"seam_and_coverage_components",qint64(checks)},{"fixed_bound",2e-6}};
}
