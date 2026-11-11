// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QCoreApplication>
#include <stdexcept>
#include "GPU_state.hh"
#include "gpu_context_private.hh"
#include "gpu_framebuffer_private.hh"
#include "gpu_state_private.hh"
#ifndef __APPLE__
#include <epoxy/gl.h>
#endif
namespace bts {
inline bool gpu_scissor_enabled(){auto *fb=blender::GPU_framebuffer_active_get();return fb&&fb->scissor_test_get();}
inline bool gpu_front_inverted(){return blender::gpu::Context::get()->state_manager->state.invert_facing;}
inline bool gpu_framebuffer_srgb(){
#ifndef __APPLE__
 return glIsEnabled(GL_FRAMEBUFFER_SRGB);
#else
 return false; // Metal encodes conversion in the attachment's format.
#endif
}
inline void gpu_restore_framebuffer_srgb(bool enabled){
#ifndef __APPLE__
 if(enabled)glEnable(GL_FRAMEBUFFER_SRGB);else glDisable(GL_FRAMEBUFFER_SRGB);
#else
 (void)enabled;
#endif
}
// Only the GL backend has ambient VAO/buffer bindings. Blender owns all Metal
// vertex/index allocation and upload state through its GPU API.
class GpuUploadBindings {
#ifndef __APPLE__
 GLint vao_=0,array_=0;GLuint upload_=0;
#endif
 public:
 GpuUploadBindings(){
#ifndef __APPLE__
 glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao_);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array_);
 glGenVertexArrays(1,&upload_);if(!upload_)throw std::runtime_error("Resident upload VAO allocation failed");glBindVertexArray(upload_);
#endif
 }
 ~GpuUploadBindings(){
#ifndef __APPLE__
 glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,array_);glDeleteVertexArrays(1,&upload_);
#endif
 }
};
}
