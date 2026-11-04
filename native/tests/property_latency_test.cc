#include "PropertyLatency.hh"
#include <QByteArray>
#include <cassert>
#include <iostream>
using namespace bts;
int main(){
 qputenv("BTS_PROPERTY_PERFORMANCE","1");property_reset_metrics();
 property_event("texture-ready");const auto texture=property_current;
 property_uploaded(texture,1);property_frame_done();
 assert(property_visible_records.size()==1&&property_current.event==0);
 // A worker completion and later unrelated strokes carry old trace copies.
 for(int i=0;i<400;++i){property_uploaded(texture,2);property_uploaded(property_current,1);property_frame_done();}
 assert(property_visible_records.size()==1&&property_waiting.event==0);
 property_event("opacity");const auto older=property_current;
 property_event("roughness");const auto latest=property_current;
 property_uploaded(latest,1);property_uploaded(older,1);property_frame_done();
 assert(property_visible_records.size()==2&&property_visible_records.last().toObject()["event"].toInteger()==qint64(latest.event));
 property_uploaded(older,1);property_frame_done();assert(property_visible_records.size()==2);
 property_event("color");const auto in_flight=property_current;property_reset_metrics();
 property_uploaded(in_flight,1);property_frame_done();assert(property_visible_records.isEmpty());
 // Completing an earlier frame must not erase a newer queued input event.
 property_event("color");const auto first=property_current;property_uploaded(first,0);
 property_event("mapping");const auto next=property_current;property_frame_done();
 assert(property_current.event==next.event);property_uploaded(next,0);property_frame_done();
 assert(property_visible_records.size()==2&&property_current.event==0);
 std::cout<<"Property first-presentation timing: stale worker/stroke traces ignored; latest input preserved; reset safe\n";
}
