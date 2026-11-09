// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include "PropertyLatency.hh"
#include "CompositeChannels.hh"
#include "TexturePadding.hh"
#include <atomic>
namespace bts {
// Private CPU arrays only. Blender Main/IDs, GPU buffers and Qt widgets stay on the owner thread.
struct NativeCompositeResult {std::uint32_t uid=0;std::uint64_t generation=0;bool preview=false;PropertyTrace trace;CompositeChannels channels=complete_composite_channels;std::map<int,std::array<std::vector<float>,17>> tiles;std::map<int,std::pair<int,int>> sizes;};
std::uint64_t native_composite_available_bytes();
std::optional<NativeCompositeResult> native_composite_job(const TextureSet &,bool ao,std::shared_ptr<TexturePadding>,std::uint64_t generation,const std::atomic_bool &cancel,bool preview=false,CompositeChannels channels=complete_composite_channels);
}
