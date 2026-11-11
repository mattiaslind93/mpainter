// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QCoreApplication>
#include "GPU_shader.hh"
#include "gpu_shader_create_info.hh"
namespace bts {
// GPUMaterial callbacks bypass GPU_shader_create_from_info_python. Raw hosted
// GLSL still needs Blender's conversion (out/inout references, matrix casts,
// built-in flags) before either backend compiles the generated source list.
inline void preprocess_hosted_sources(blender::gpu::shader::ShaderCreateInfo &info){
 // Keep native authoring values reproducible across projection variants.
 info.define("BTS_PRECISE_MATH");
 for(auto &source:info.generated_sources)
  source.content=blender::GPU_shader_preprocess_source(source.content.c_str(),info);
}
}
