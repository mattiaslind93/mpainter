#pragma once
#include "ChannelPrecision.hh"
#include "FillTexture.hh"
#include <QJsonObject>
#include <cmath>
#include <algorithm>
#include <stdexcept>
namespace bts {
inline constexpr int displacement_effect_channel=14;
inline constexpr int displacement_image_channel=16; // 15 remains selected-mask presentation.
struct DisplacementSettings {
  bool channel=false,allocated=false,enabled=true;
  float strength=1,midlevel=0;
  int subdivision=2,mode=0; // Stable values: 0 Uniform/Simple; 1 Edge Length (object-space uniform approximation); 2 Catmull-Clark.
  double max_length=1./64;
  ChannelPrecision precision=ChannelPrecision::L32F;
  bool operator==(const DisplacementSettings &)const=default;
  void validate()const {
    if((channel&&!allocated)||!std::isfinite(strength)||strength<0||strength>1e6||!std::isfinite(midlevel)||midlevel<0||midlevel>1||subdivision<0||subdivision>11||mode<0||mode>2||!std::isfinite(max_length)||max_length<1e-6||max_length>1e6||int(precision)<0||int(precision)>8)throw std::invalid_argument("Invalid displacement settings");
  }
  float bounds()const{return strength*std::max(std::abs(1-midlevel),std::abs(midlevel))*1.05f+1e-6f;}
};
// Only invoked by explicit image assignment, never by Open/hydration.
void displacement_import_range(DisplacementSettings &,const FillTexture &);
QJsonObject displacement_json(const DisplacementSettings &);
DisplacementSettings read_displacement(const QJsonValue &);
}
