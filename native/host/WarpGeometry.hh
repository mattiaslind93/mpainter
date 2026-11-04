#pragma once
#include "WarpGrid.hh"
#include <functional>
#include <memory>
#include <cstdint>
namespace bts {
struct WarpFitFrame {
 WarpVector origin{};
 std::array<WarpVector,3> axes{{{1,0,0},{0,1,0},{0,0,1}}};
 WarpVector world(const WarpVector &p)const{auto result=origin;for(int i=0;i<3;++i)result=warp_add(result,axes[i],p[i]);return result;}
 double determinant()const{return warp_dot(axes[0],warp_cross(axes[1],axes[2]));}
 WarpVector local(const WarpVector &p)const{const auto delta=warp_add(p,origin,-1);const double det=determinant();return {warp_dot(delta,warp_cross(axes[1],axes[2]))/det,warp_dot(delta,warp_cross(axes[2],axes[0]))/det,warp_dot(delta,warp_cross(axes[0],axes[1]))/det};}
 WarpVector front()const{return warp_normal(warp_scale(warp_cross(axes[0],axes[2]),1/determinant()));}
 void validate()const{for(double v:origin)if(!std::isfinite(v))throw std::invalid_argument("Invalid warp fit origin");for(const auto &axis:axes)for(double v:axis)if(!std::isfinite(v))throw std::invalid_argument("Invalid warp fit frame");const double size=std::sqrt(warp_dot(axes[0],axes[0])*warp_dot(axes[1],axes[1])*warp_dot(axes[2],axes[2]));if(!std::isfinite(size)||size<=0||!std::isfinite(determinant())||std::abs(determinant())<size*1e-12)throw std::invalid_argument("Singular warp fit frame");}
};
using WarpSurfaceRay=std::function<std::optional<WarpVector>(const WarpVector &,const WarpVector &,double)>;
struct WarpFitResult {std::shared_ptr<const WarpGrid> grid;int hits=0,misses=0;};
inline WarpFitResult fit_warp_geometry(const WarpGrid &authored,const WarpFitFrame &frame,double reach,const WarpSurfaceRay &ray)
{
 authored.validate();frame.validate();if(!std::isfinite(reach)||reach<=0||!ray)throw std::invalid_argument("Invalid warp fit query");
 auto fitted=std::make_shared<WarpGrid>(authored);const auto front=frame.front();const auto direction=warp_scale(front,-1);WarpFitResult result;
 for(int y=0;y<authored.height();++y)for(int x=0;x<authored.width();++x){const auto &source=authored.at(x,y);auto reference=source.position;const double depth=reference[1]+.5;reference[1]=-.5;const auto base=frame.world(reference),origin=warp_add(base,front,reach);
   const auto hit=ray(origin,direction,2*reach);bool valid=bool(hit);if(hit){for(double v:*hit)valid&=std::isfinite(v);const auto delta=warp_add(*hit,origin,-1);const double distance=warp_dot(delta,direction);valid&=distance>=0&&distance<=2*reach;}
   if(!valid){++result.misses;continue;}++result.hits;auto position=frame.local(*hit);position[1]+=depth;fitted->at(x,y).position=position;
 }
 if(result.hits==0){result.grid=std::make_shared<const WarpGrid>(authored);return result;}
 // Retain custom tangent edits as residuals from the authored grid's own smooth tangents.
 auto automatic=authored;automatic.align_tangents();fitted->align_tangents();if(!authored.automatic_tangents)for(std::size_t i=0;i<authored.nodes.size();++i){fitted->nodes[i].du=warp_add(fitted->nodes[i].du,warp_add(authored.nodes[i].du,automatic.nodes[i].du,-1));fitted->nodes[i].dv=warp_add(fitted->nodes[i].dv,warp_add(authored.nodes[i].dv,automatic.nodes[i].dv,-1));fitted->nodes[i].duv=warp_add(fitted->nodes[i].duv,warp_add(authored.nodes[i].duv,automatic.nodes[i].duv,-1));}fitted->automatic_tangents=authored.automatic_tangents;fitted->validate();result.grid=std::move(fitted);return result;
}
// View editing occurs on a fitted surface. Convert the visible delta back into
// authored data, including insertion of UV knots, before the next fit.
inline std::shared_ptr<const WarpGrid> rebase_warp_edit(const WarpGrid &authored,const WarpGrid &fitted,const WarpGrid &edited)
{
 authored.validate();fitted.validate();edited.validate();auto result=std::make_shared<WarpGrid>(edited);
 for(int y=0;y<edited.height();++y)for(int x=0;x<edited.width();++x){const double u=edited.columns[x],v=edited.rows[y];const auto base=authored.evaluate(u,v),before=fitted.evaluate(u,v);auto &node=result->at(x,y);const auto &after=edited.at(x,y);node.position=warp_add(base.position,warp_add(after.position,before.position,-1));node.du=warp_add(base.du,warp_add(after.du,before.du,-1));node.dv=warp_add(base.dv,warp_add(after.dv,before.dv,-1));node.duv=warp_add(base.duv,warp_add(after.duv,before.duv,-1));}
 // Keep exact Hermite derivatives when UV knots were inserted; resmoothing
 // here would change an otherwise identical authored patch.
 result->validate();return result;
}
struct WarpGeometryCache {
 std::shared_ptr<const WarpGrid> authored;
 WarpFitFrame frame;WarpVector center{},offset{},rotation{},scale{1,1,1},physical_size{};
 double extent=1,reach=1,cm_per_native=1;bool physical=false;
 std::uint64_t generation=0;std::uint32_t material=0;
 WarpFitResult fitted;
};
struct WarpGeometrySettings {
 bool enabled=false;
 // Retained only while off: preserve visible conformance without turning it
 // into an accidental second authored deformation when automatic fitting resumes.
 std::shared_ptr<const WarpGrid> authored_reference,frozen_reference;
 void validate()const{if(bool(authored_reference)!=bool(frozen_reference)||(enabled&&authored_reference))throw std::invalid_argument("Invalid warp geometry references");if(authored_reference){authored_reference->validate();frozen_reference->validate();}}
};
struct WarpGeometryTransition {std::shared_ptr<const WarpGrid> grid;std::shared_ptr<const WarpGeometrySettings> settings;};
inline WarpGeometryTransition change_warp_geometry_mode(const std::shared_ptr<const WarpGrid> &grid,const std::shared_ptr<const WarpGeometrySettings> &before,const std::shared_ptr<const WarpGrid> &fitted,bool enabled)
{
 if(!grid)throw std::invalid_argument("Warp geometry needs an authoring grid");grid->validate();if(before)before->validate();if(enabled==bool(before&&before->enabled))return {grid,before};
 auto settings=std::make_shared<WarpGeometrySettings>();settings->enabled=enabled;
 if(enabled){auto author=before&&before->authored_reference?rebase_warp_edit(*before->authored_reference,*before->frozen_reference,*grid):grid;return {std::move(author),std::move(settings)};}
 settings->authored_reference=grid;settings->frozen_reference=fitted?fitted:grid;settings->validate();return {settings->frozen_reference,settings};
}

}
