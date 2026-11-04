#pragma once
#include <QUuid>
#include <array>
#include <algorithm>
#include <memory>
#include <set>
#include <cstdint>
#include <optional>
#include <string>
#include <stdexcept>
namespace bts {
inline constexpr std::uint64_t adjustment_page_bytes=64ULL*64*16;
struct AdjustmentPaintMemoryError:std::length_error{
 std::uint64_t needed=2*adjustment_page_bytes,available;
 explicit AdjustmentPaintMemoryError(std::uint64_t value):std::length_error("Adjustment brush needs "+std::to_string(double(2*adjustment_page_bytes)/(1024*1024))+" MiB for active source pages; available "+std::to_string(double(value)/(1024*1024))+" MiB. Free GPU cache memory or undo the stroke."),available(value){}
};
inline void require_adjustment_page_pair(std::uint64_t available){if(available<2*adjustment_page_bytes)throw AdjustmentPaintMemoryError(available);}
struct AdjustmentPaintFailure{QUuid snapshot,set;std::string message;std::uint64_t needed=2*adjustment_page_bytes,available=0;QUuid document,layer;};
// Owner-thread delivery only, consumed by the workspace transaction boundary.
inline std::optional<AdjustmentPaintFailure> pending_adjustment_failure;
// Metadata token only. GPU allocations and destruction stay on the host owner.
struct AdjustmentPaintSnapshot { QUuid id,set,layer,document; std::set<int> tiles; };
struct AdjustmentPaintSource {
 std::shared_ptr<const AdjustmentPaintSnapshot> snapshot;
 // Physical five-plane paint payload -> logical compositor channel. -1 uses own paint only.
 std::array<int,5> channels{-1,-1,-1,-1,-1};
 bool enabled(int channel)const{return snapshot&&channels.at(channel)>=0;}
 explicit operator bool()const{return snapshot&&std::any_of(channels.begin(),channels.end(),[](int c){return c>=0;});}
};
}
