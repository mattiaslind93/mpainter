// SPDX-License-Identifier: GPL-3.0-or-later
#include "UvTransfer.hh"
#include <cassert>
#include <iostream>
int main(){bts::UvFaceTriangle a;a.positions={{{0,0,0},{1,0,0},{0,1,0}}};a.corners={{{0,0},{1,0},{0,1}}};auto b=a;for(auto &uv:b.corners)uv[0]=1-uv[0];auto matrix=bts::uv_normal_transform(bts::uv_tangent_frame(a),bts::uv_tangent_frame(b));assert(matrix[0]==-1&&matrix[4]==1&&matrix[8]==1);b=a;for(auto &uv:b.corners){const auto x=uv[0];uv[0]=uv[1];uv[1]=1-x;}matrix=bts::uv_normal_transform(bts::uv_tangent_frame(a),bts::uv_tangent_frame(b));assert(matrix[1]==1&&matrix[3]==-1&&matrix[8]==1);std::cout<<"Mirrored and rotated UV tangent transforms passed\n";}
