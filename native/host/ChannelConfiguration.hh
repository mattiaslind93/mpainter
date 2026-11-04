#pragma once
#include "TextureDocument.hh"
#include <optional>
class QWidget;
namespace bts {
// Stable IDs match the native layer API: 0..5 material, 6..13 custom slots, 14 displacement.
struct ChannelConfiguration {
  struct Entry {bool enabled=false;ChannelPrecision precision=ChannelPrecision::L32F;bool operator==(const Entry &)const=default;};
  std::array<Entry,15> channels;
  bool operator==(const ChannelConfiguration &)const=default;
};
struct ChannelConfigurationEdit {ChannelConfiguration configuration;bool all_sets=false;};
ChannelConfiguration channel_configuration(const TextureSet &);
// Transactional even when a later set fails its allocation budget.
std::vector<std::uint32_t> apply_channel_configuration(TextureDocument &,const QUuid &,const ChannelConfigurationEdit &);
std::optional<ChannelConfigurationEdit> choose_channel_configuration(QWidget *,const TextureDocument &,const TextureSet &);
}
