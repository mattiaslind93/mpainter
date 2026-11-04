#pragma once
#include "NativeWorldNormalUv.hh"
#include <QJsonObject>
namespace bts {
inline QJsonObject verify_native_world_normal_uv(){
 const NativeWorldUvTriangle a{{{0,0},{1,0},{0,1}}},edge{{{1,0},{1,1},{0,1}}},vertex{{{1,0},{2,0},{1,1}}},inside{{{.125f,.125f},{.25f,.125f},{.125f,.25f}}},partial{{{.5f,0},{1.5f,0},{.5f,1}}};
 const auto require=[](bool ok){if(!ok)throw std::runtime_error("Native world strict UV interior overlap differs");};
 require(!native_world_uv_interior_overlap(a,edge));require(!native_world_uv_interior_overlap(edge,a));require(!native_world_uv_interior_overlap(a,vertex));require(native_world_uv_interior_overlap(a,a));require(native_world_uv_interior_overlap(a,inside));require(native_world_uv_interior_overlap(inside,a));require(native_world_uv_interior_overlap(a,partial));auto mirrored=a;std::swap(mirrored[1],mirrored[2]);require(native_world_uv_interior_overlap(a,mirrored));
 const NativeWorldUvTriangle shader_ball_56{{{.18525800108909607f,.9839400053024292f},{.18748599290847778f,.9909719824790955f},{.18987999856472015f,.9911500215530396f}}};
 const NativeWorldUvTriangle shader_ball_28{{{.19415099918842316f,.9780809879302979f},{.18525800108909607f,.9839400053024292f},{.1898770034313202f,.991150975227356f}}};
 require(native_world_uv_interior_overlap(shader_ball_56,shader_ball_28));
 return {{"cases",9},{"shared_edges_and_vertices_allowed",true},{"absolute_world_overlapping_interiors_rejected",true},{"recovery_shader_ball_seam_overlap",true},{"pixel_evaluation",false}};
}
}
