#pragma once
#include <vector>
namespace bts {
// Input/output use associated RGBA; native ImBuf filtering preserves UV-edge coverage.
std::vector<float> filter_mesh_bake(std::vector<float>,int source_width,int source_height,
                                  int width,int height,int dilation,bool diffusion,int role);
}
