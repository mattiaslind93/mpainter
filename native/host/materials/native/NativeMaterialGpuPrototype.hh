// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace blender {
struct Main;
}
namespace bts {
void verify_native_material_gpu_prototype(const blender::Main &);
}
