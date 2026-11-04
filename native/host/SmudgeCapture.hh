#pragma once
#include "TilePaintPixels.hh"
#include "PaintRecording.hh"
#include "AdjustmentPaintSource.hh"
#include <mutex>
namespace bts {
// Immutable inputs shared with Undo. CPU evaluation is only an explicit
// reference/export boundary; GPU consumers inspect these recipe fields.
struct SmudgeCapture final : SmudgePayloadRecipe {
 TilePaintPixels source;
 PaintPixels::Stamp footprint;
 int resolution;
 AdjustmentPaintSource adjustment;
 SmudgeCapture(const TilePaintPixels &pixels,const PaintPixels::Stamp &stamp,int size,AdjustmentPaintSource input={})
     :SmudgePayloadRecipe(Kind::Capture),source(pixels),footprint(stamp),resolution(size),adjustment(std::move(input)){if(stamp.surface)footprint.surface=std::make_shared<PaintPixels::SurfaceFootprint>(*stamp.surface);}
 void cpu_sources(const std::function<void(const PaintPixels &)> &visit)const override{for(const auto &[tile,pixels]:source.tiles())visit(pixels);}
 bool has_ink(int channel)const override{PaintMetadataScope metadata;return footprint.channels[channel]&&(adjustment.enabled(channel)||source.page_count(channel)>0);}
 const SmudgePayload &evaluated() const override;
 private:
 mutable std::once_flag once_;
 mutable std::shared_ptr<const SmudgePayload> result_;
};
struct SmudgeCarry final : SmudgePayloadRecipe {
 std::shared_ptr<const SmudgePayload> current,previous;
 float pickup;
 SmudgeCarry(std::shared_ptr<const SmudgePayload> now,std::shared_ptr<const SmudgePayload> before,float amount)
     :SmudgePayloadRecipe(Kind::Carry),current(std::move(now)),previous(std::move(before)),pickup(amount){}
 // The predecessor stamp has already evaluated the carried payload. Keep its
 // newly picked-up source alive in the CPU scratch DAG until this stamp uses it.
 void cpu_sources(const std::function<void(const PaintPixels &)> &visit)const override{if(current->recipe)current->recipe->cpu_sources(visit);}
 bool has_ink(int channel)const override{return current->has_ink(channel)||previous->has_ink(channel);}
 const SmudgePayload &evaluated() const override;
 private:
 mutable std::once_flag once_;
 mutable std::shared_ptr<const SmudgePayload> result_;
};
}
