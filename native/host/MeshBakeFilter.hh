#pragma once
#include <vector>
#include <span>
#include "FillProjection.hh"
namespace bts {
void denoise_mesh_bake(std::vector<float> &,std::span<const FillSurfacePoint>,int width,int height);
// Input/output use associated RGBA; native ImBuf filtering preserves UV-edge coverage.
std::vector<float> filter_mesh_bake(std::vector<float>,int source_width,int source_height,
                                  int width,int height,int dilation,bool diffusion,int role);
}
