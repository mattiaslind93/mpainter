#include "Theme.hh"
#pragma once
#include "BrushSettings.hh"
#include <QImage>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <optional>
namespace bts {
// Only schedules immutable snapshots and displays the owner's GPU result.
// It never stamps, samples or evaluates project/CPU pixels.
class BrushPreview final:public QWidget {
 public:
  using Renderer=std::function<void(const BrushSettings &,std::function<void(QImage)>)>;
  explicit BrushPreview(QWidget *parent):QWidget(parent){setObjectName("brushStrokePreview");setAccessibleName("Brush stroke preview");setFixedHeight(96);setMinimumWidth(120);}
  void set_renderer(Renderer render){render_=std::move(render);queue();}
  void synchronize(const BrushSettings &brush){auto key=signature(brush);if(desired_&&*desired_==key)return;desired_=key;if(displayed_&&*displayed_==key)pending_.reset();else pending_=std::pair{std::move(key),brush};queue();}
  const QImage &image()const{return image_;}
  int generations()const{return generations_;}
 protected:
  void paintEvent(QPaintEvent *)override {QPainter painter(this);painter.fillRect(rect(),QColor(bts::theme::Surface));if(!image_.isNull())painter.drawImage(rect(),image_);}
 private:
  using Key=std::pair<std::vector<double>,std::array<std::shared_ptr<const AlphaBitmap>,3>>;
  static Key signature(const BrushSettings &b){Key key;auto &v=key.first;const auto value=[&](auto n){v.push_back(double(n));};const auto values=[&](const MaterialValues &m){for(auto n:m.base_color)value(n);value(m.metallic);value(m.roughness);for(auto n:m.normal)value(n);value(m.bump);value(m.ambient_occlusion);};
   const auto alpha=[&](const BrushAlpha &a,int slot){value(a.kind);value(a.inverted);value(a.uniform);value(a.dynamic.cycle);value(a.dynamic.random_start);value(a.dynamic.roundness);value(a.dynamic.width);value(a.dynamic.phase);key.second[slot]=a.bitmap;};
   const auto stencil=[&](const BrushStencil &s,int slot){value(bool(s.resource));if(s.resource)alpha(*s.resource,slot);value(s.tiling);for(auto n:s.offset)value(n);value(s.scale);value(s.angle);value(s.overlay);};
   for(auto n:b.users)value(n);for(auto n:b.user_values)value(n);values(b.values);for(auto n:b.channels)value(n);
   value(b.erase);value(b.ao);value(b.smudge);value(b.clone);value(b.clone_absolute);value(b.displacement);value(b.displacement_value);alpha(b.alpha,0);stencil(b.stencil,1);value(bool(b.projected));if(b.projected){stencil(b.projected->transform,2);values(b.projected->values);value(b.projected->weave);value(b.projected->density);}
   value(b.projection.alignment);value(b.projection.cull);value(b.projection.angle);value(b.projection.orientation);value(b.projection.symmetry_ray);for(auto n:b.projection.camera_right)value(n);for(auto n:b.projection.camera_up)value(n);for(auto n:b.jitter.values())value(n);
   for(auto n:{b.angle,float(b.follow_path),b.hardness,b.mask_value,b.diameter,float(b.pressure_size),float(b.pressure_flow),b.minimum_size,b.minimum_flow,float(b.lazy_mouse),b.lazy_distance,b.spacing,b.stroke_opacity,b.flow})value(n);return key;}
  Renderer render_;QImage image_;std::optional<Key> desired_,displayed_;std::optional<std::pair<Key,BrushSettings>> pending_;bool queued_=false,in_flight_=false;std::uint64_t serial_=0;int generations_=0;
  void queue(){if(queued_||in_flight_||!pending_||!render_)return;queued_=true;QTimer::singleShot(0,this,[this]{queued_=false;if(in_flight_||!pending_||!render_)return;auto request=std::move(*pending_);pending_.reset();in_flight_=true;const auto serial=++serial_;QPointer<BrushPreview> self=this;render_(request.second,[self,key=std::move(request.first),serial](QImage result){if(!self||self->serial_!=serial)return;self->in_flight_=false;if(self->desired_&&*self->desired_==key){self->image_=std::move(result);self->displayed_=key;if(self->pending_&&self->pending_->first==key)self->pending_.reset();++self->generations_;self->update();}self->queue();});});}
};
}
