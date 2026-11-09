// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChannelConfiguration.hh"
namespace bts {
ChannelConfiguration channel_configuration(const TextureSet &set)
{
  ChannelConfiguration result;
  for (int c = 0; c < 6; ++c)
    result.channels[c] = {c < 5 ? set.channels[c] : set.ao_channel, set.precision[c]};
  for (int slot = 0; slot < custom_channel_count; ++slot)
    result.channels[6 + slot] = {set.users[slot].present, set.users[slot].precision};
  result.channels[14] = {set.displacement.channel, set.displacement.precision};
  return result;
}
std::vector<std::uint32_t> apply_channel_configuration(TextureDocument &document,
                                                       const QUuid &source,
                                                       const ChannelConfigurationEdit &edit)
{
  const auto found = std::find_if(document.texture_sets().begin(),
                                  document.texture_sets().end(),
                                  [&](const auto &set) { return set.id == source; });
  if (found == document.texture_sets().end() || found->disabled || !found->uniform_editable)
    throw std::invalid_argument("Choose an editable Texture Set");
  for (const auto &entry : edit.configuration.channels)
    if (int(entry.precision) < 0 || int(entry.precision) > 8)
      throw std::invalid_argument("Invalid channel precision");
  auto candidate = document;
  std::vector<std::uint32_t> changed;
  for (const auto &original : document.texture_sets()) {
    if (original.disabled || !original.uniform_editable ||
        (!edit.all_sets && original.id != source))
      continue;
    if (channel_configuration(original) == edit.configuration)
      continue;
    auto &set = candidate.set(original.material_session_uid);
    // Release presence first so replacing a channel does not transiently exceed the budget.
    for (int slot = 0; slot < custom_channel_count; ++slot)
      set.users[slot].present = false;
    set.displacement.channel = false;
    for (int c = 0; c < 6; ++c) {
      const auto entry = edit.configuration.channels[c];
      if (c < 5)
        set.channels[c] = entry.enabled;
      else
        set.ao_channel = entry.enabled;
      set.precision[c] = entry.precision;
    }
    if (!set.channels[3])
      set.normal_mixing = NormalMixing::Combine;
    if (!set.channels[4])
      set.bump_method = BumpMethod::SmoothSobel;
    for (int slot = 0; slot < custom_channel_count; ++slot) {
      const auto entry = edit.configuration.channels[6 + slot];
      if (entry.enabled)
        TextureDocument::add_user_channel(set, slot);
      set.users[slot].precision = entry.precision;
    }
    if (edit.configuration.channels[14].enabled)
      TextureDocument::add_displacement_channel(set);
    set.displacement.precision = edit.configuration.channels[14].precision;
    changed.push_back(set.material_session_uid);
  }
  document = std::move(candidate);
  return changed;
}
}  // namespace bts
