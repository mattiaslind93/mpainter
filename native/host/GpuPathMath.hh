#pragma once
namespace bts {
inline constexpr char gpu_path_math[]=R"GLSL(
// Two-float predicates retain the CPU contour side at near-collinear texels.
// This uses ordinary FP32/FMA and is portable to GPU backends without FP64.
vec2 ds_add(vec2 a,vec2 b){precise float s=a.x+b.x;precise float v=s-a.x;precise float e=(a.x-(s-v))+(b.x-v)+a.y+b.y;precise float h=s+e;return vec2(h,e-(h-s));}
vec2 ds_sub(vec2 a,vec2 b){return ds_add(a,-b);}
vec2 ds_mul(vec2 a,vec2 b){precise float p=a.x*b.x;precise float e=fma(a.x,b.x,-p)+a.x*b.y+a.y*b.x+a.y*b.y;precise float h=p+e;return vec2(h,e-(h-p));}
vec2 ds_div(vec2 a,vec2 b){float q=a.x/b.x;vec2 r=ds_sub(a,ds_mul(vec2(q,0),b));return ds_add(vec2(q,0),vec2((r.x+r.y)/b.x,0));}
bool ds_less(vec2 a,vec2 b){vec2 d=ds_sub(a,b);return d.x<0||(d.x==0&&d.y<0);}
vec2 ds_dot(vec2 x,vec2 y,vec2 z,vec3 hi,vec3 lo){return ds_add(ds_add(ds_mul(x,vec2(hi.x,lo.x)),ds_mul(y,vec2(hi.y,lo.y))),ds_mul(z,vec2(hi.z,lo.z)));}
)GLSL";
}
