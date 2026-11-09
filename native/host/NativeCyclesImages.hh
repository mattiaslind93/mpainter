// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>
#include <string_view>
class QJsonObject;
namespace blender {struct Main;struct Image;}
namespace bts {
// GPU-owner coordinator. Only completed target writes advance revisions;
// workers receive frozen CPU data and immutable metadata, never Blender IDs.
enum class NativeCyclesImageRole : int { NativeWorldFront=100,NativeWorldBack=101 };
class NativeCyclesImages {
 struct Impl;std::unique_ptr<Impl> impl_;
 public:
 NativeCyclesImages();~NativeCyclesImages();
 void changed(blender::Image &,std::uint32_t material_uid);
 void changed(blender::Image &,std::uint32_t material_uid,int channel_role);
 void changed(blender::Image &image,std::uint32_t material_uid,NativeCyclesImageRole role){changed(image,material_uid,int(role));}
 bool active()const;
 bool image_ready(std::uint32_t image_uid)const;
 std::uint64_t entry_epoch()const;
 bool update(blender::Main &,bool active);
 bool pending()const;
 std::string_view error()const;
 // Explicit gated diagnostic: bounded samples of successful publication packets.
 QJsonObject diagnostic_publication_packets()const;
 void reset(); // GPU owner; joins CPU preparation before releasing snapshots.
};
}
