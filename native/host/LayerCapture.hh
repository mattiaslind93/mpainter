#pragma once
#include <QUuid>
#include <array>
#include <vector>
#include <memory>
#include <cstdint>
namespace bts {
// Explicit bitmap bake/export packet, not a viewport readback. At most 256²
// texels are transferred after a zero-timeout GPU fence signals completion.
struct LayerCaptureRequest {
 QUuid layer;
 int channel=0,tile=1001,x=0,y=0,width=0,height=0,canvas_width=0,canvas_height=0;
 bool mask=false,contextual=false;
 bool normal_perturbation=false; // Export-only normal stack, before scalar Height derivation.
 bool final_composite=false,authored_ao=false; // Explicit export only; channel is logical0..16.
};
struct LayerCaptureResult {
 std::uint64_t ticket=0;
 bool valid=true;
 std::shared_ptr<std::vector<std::array<float,4>>> pixels;
};
}
