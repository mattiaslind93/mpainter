#pragma once
namespace bts::gpu_warp {
// Same bounded Hermite projection contract as WarpGrid::project. Only the
// small immutable knot/control-point array is supplied by the CPU.
inline const char *source = R"GLSL(
vec4 warp_data(int index){return texelFetch(layer_parameters_img,ivec2(WARP_DATA_START+index,0),0);}
struct WarpBasis {vec2 value;vec2 tangent;vec2 derivative;vec2 tangent_derivative;vec2 second;vec2 tangent_second;};
WarpBasis warp_basis(float t,float span){float t2=t*t,t3=t2*t;WarpBasis b;b.value=vec2(2*t3-3*t2+1,-2*t3+3*t2);b.tangent=vec2(t3-2*t2+t,t3-t2)*span;b.derivative=vec2(6*t2-6*t,-6*t2+6*t)/span;b.tangent_derivative=vec2(3*t2-4*t+1,3*t2-2*t);b.second=vec2(12*t-6,-12*t+6)/(span*span);b.tangent_second=vec2(6*t-4,6*t-2)/span;return b;}
struct WarpSurfaceValue {vec3 position;vec3 du;vec3 dv;vec3 duv;vec3 duu;vec3 dvv;vec3 duuv;vec3 duvv;};
WarpSurfaceValue warp_evaluate(vec2 uv){
 vec2 q=clamp(uv,vec2(0),vec2(1)),extra=uv-q;ivec2 size=ivec2(warp_info.xy),at=size-2;
 for(int i=0;i<size.x-1;i++)if(q.x<warp_data(i+1).x){at.x=i;break;}
 for(int i=0;i<size.y-1;i++)if(q.y<warp_data(9+i+1).x){at.y=i;break;}
 vec2 start=vec2(warp_data(at.x).x,warp_data(9+at.y).x),span=vec2(warp_data(at.x+1).x,warp_data(9+at.y+1).x)-start;
 WarpBasis a=warp_basis((q.x-start.x)/span.x,span.x),b=warp_basis((q.y-start.y)/span.y,span.y);
 WarpSurfaceValue result_value;result_value.position=vec3(0);result_value.du=vec3(0);result_value.dv=vec3(0);result_value.duv=vec3(0);result_value.duu=vec3(0);result_value.dvv=vec3(0);result_value.duuv=vec3(0);result_value.duvv=vec3(0);
 int corners=min(int(warp_info.x),2);
 for(int j=0;j<corners;j++)for(int i=0;i<corners;i++)for(int v=0;v<corners;v++)for(int u=0;u<corners;u++){
  int index=18+((at.y+j)*size.x+at.x+i)*4+(u!=0?(v!=0?3:1):(v!=0?2:0));vec3 value=warp_data(index).xyz;
  float au=u!=0?a.tangent[i]:a.value[i],bu=u!=0?a.tangent_derivative[i]:a.derivative[i],av=v!=0?b.tangent[j]:b.value[j],bv=v!=0?b.tangent_derivative[j]:b.derivative[j];
  result_value.position+=value*(au*av);result_value.du+=value*(bu*av);result_value.dv+=value*(au*bv);result_value.duv+=value*(bu*bv);float cu=u!=0?a.tangent_second[i]:a.second[i],cv=v!=0?b.tangent_second[j]:b.second[j];result_value.duu+=value*(cu*av);result_value.dvv+=value*(au*cv);result_value.duuv+=value*(cu*bv);result_value.duvv+=value*(bu*cv);
 }
 result_value.position+=result_value.du*extra.x+result_value.dv*extra.y+result_value.duv*(extra.x*extra.y);result_value.du+=result_value.duv*extra.y;result_value.dv+=result_value.duv*extra.x;result_value.duu=extra.x!=0?vec3(0):result_value.duu+result_value.duuv*extra.y;result_value.dvv=extra.y!=0?vec3(0):result_value.dvv+result_value.duvv*extra.x;return result_value;
}
float warp_tangential_squared(WarpSurfaceValue value,vec3 residual,vec3 stretch){
 vec3 normal=cross(value.du*stretch,value.dv*stretch);float magnitude=length(normal);if(magnitude<1e-12)return 1e30;normal/=magnitude;vec3 r=residual*stretch,tangent=r-normal*dot(r,normal);return dot(tangent,tangent);
}
float warp_projection_error=1e9;
bool warp_project(vec3 point,out vec2 uv,out float depth,out vec3 normal,out vec3 derivative_u,out vec3 derivative_v){
 vec3 ratio=inverse_denominator.y/inverse_denominator,metric=ratio*ratio,stretch=abs(ratio);
 if(any(lessThan(metric,vec3(1e-12)))||any(greaterThan(metric,vec3(1e12)))||any(greaterThan(abs(point),vec3(1e7))))return false;
 uv=point.xz+.5;WarpSurfaceValue value;
 if(warp_identity!=0){value.position=vec3(point.x,-.5,point.z);value.du=vec3(1,0,0);value.dv=vec3(0,0,1);value.duv=vec3(0);value.duu=vec3(0);value.dvv=vec3(0);value.duuv=vec3(0);value.duvv=vec3(0);}
 else{
  for(int iteration=0;iteration<64;iteration++){
   value=warp_evaluate(uv);vec3 residual=point-value.position;precise float a=dot(value.du*metric,value.du),b=dot(value.du*metric,value.dv),c=dot(value.dv*metric,value.dv),det=a*c-b*b;if(det<1e-16)return false;
   precise float ha=a-dot(residual*metric,value.duu),hb=b-dot(residual*metric,value.duv),hc=c-dot(residual*metric,value.dvv),hd=ha*hc-hb*hb;bool convex=ha>1e-12&&hc>1e-12&&hd>1e-16;if(convex){a=ha;b=hb;c=hc;det=hd;}
   precise float x=dot(residual*metric,value.du),y=dot(residual*metric,value.dv);precise vec2 step=vec2(c*x-b*y,a*y-b*x)/det;step/=max(1.0,2*max(abs(step.x),abs(step.y)));if(length(step)<1e-9||all(equal(uv+step,uv)))break;
   precise float old_distance=dot(residual*metric,residual);float old_tangent=warp_tangential_squared(value,residual,stretch);bool accepted=false;
   // Distance to the surface includes a large normal component. At float32
   // precision its squared length stops resolving small tangent improvements.
   // Within that rounding interval, compare the tangent residual directly;
   // never relax the final 1e-5 projection validity threshold.
   // Surface evaluation has absolute coordinate error, not merely an error
   // relative to the small residual. Include that subtraction uncertainty;
   // otherwise near-surface points can reject an improving tangent step.
   float coordinate_scale=max(1.0,max(length(point),length(value.position)));
   float uncertainty=16*1.1920928955078125e-7*(old_distance+sqrt(old_distance)*coordinate_scale);
   for(int attempt=0;attempt<10;attempt++){
    vec2 next_uv=clamp(uv+step*exp2(-float(attempt)),vec2(-32),vec2(33));if(all(equal(next_uv,uv)))break;WarpSurfaceValue next_value=warp_evaluate(next_uv);precise vec3 error=point-next_value.position;precise float next_distance=dot(error*metric,error);
    bool lower=next_distance<old_distance-uncertainty;
    // A descent direction escapes a saddle by increasing the gradient.
    // Only demand a smaller tangent residual in a locally convex patch;
    // otherwise float32 distance ties would stop that legitimate descent.
    bool roundoff=abs(next_distance-old_distance)<=uncertainty&&(!convex||warp_tangential_squared(next_value,error,stretch)<old_tangent);
    if(lower||roundoff){uv=next_uv;accepted=true;break;}
   }
   if(!accepted)break;
  }
  value=warp_evaluate(uv);
 }
 normal=cross(value.du*stretch,value.dv*stretch);float length_normal=length(normal);if(length_normal<1e-12)return false;normal/=length_normal;
 vec3 residual=(point-value.position)*stretch;depth=dot(residual,normal);warp_projection_error=length(residual-normal*depth);if(warp_projection_error>1e-5)return false;derivative_u=value.du;derivative_v=value.dv;return true;
}
)GLSL";
}  // namespace bts::gpu_warp
