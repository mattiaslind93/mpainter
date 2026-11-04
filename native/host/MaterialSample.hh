#pragma once
#include "MaterialValues.hh"
#include <QUuid>
#include <array>
#include <cstdint>
namespace bts {
// Scene-linear authoring values at the project's channel precision. No lighting,
// display transform, baked AO or bump-derived normals enter a material pick.
struct MaterialSampleRequest {
 int tile=1001,x=0,y=0;
 QUuid mask;
 std::array<bool,6> channels{true,true,true,true,true,true};
};
struct MaterialSample {
 MaterialValues material;
 float mask=1;
 std::uint64_t ticket=0;
 bool valid=true;
};
}
