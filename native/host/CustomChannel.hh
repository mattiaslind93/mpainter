#pragma once
#include "ChannelPrecision.hh"
#include "LayerBlend.hh"
#include "StrokeHistory.hh"
#include <QJsonArray>
#include <QJsonObject>
#include <array>
#include <string>
namespace bts {
inline constexpr int custom_channel_count=8;
struct CustomChannel {bool present=false,allocated=false;bool scattering=false;std::string name;ChannelPrecision precision=ChannelPrecision::L32F;};
inline std::array<CustomChannel,custom_channel_count> default_custom_channels(){std::array<CustomChannel,custom_channel_count> result;for(int i=0;i<custom_channel_count;++i)result[i].name="User"+std::to_string(i);return result;}
struct CustomLayer {bool enabled=true;float value=1,opacity=1;LayerBlend blending=LayerBlend::Normal;int author_resolution=0;TilePaintPixels pixels;std::shared_ptr<const StrokeHistory> strokes;};
inline int custom_authoring_resolution(const CustomLayer &layer,int fallback){return layer.author_resolution?layer.author_resolution:layer.pixels.resolution()?layer.pixels.resolution():fallback;}
QJsonArray custom_channels_json(const std::array<CustomChannel,custom_channel_count> &);
std::array<CustomChannel,custom_channel_count> read_custom_channels(const QJsonValue &);
QJsonArray custom_layers_json(const std::array<CustomLayer,custom_channel_count> &);
std::array<CustomLayer,custom_channel_count> read_custom_layers(const QJsonValue &,const std::set<int> &tiles);
QJsonObject custom_layer_json(const CustomLayer &);
CustomLayer read_custom_layer(const QJsonValue &,const std::set<int> &tiles);
}
