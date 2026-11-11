// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GPU_storage_buffer.hh"
#include "GPU_storage_buffer_poll.hh"
#include <stdexcept>
#ifndef __APPLE__
#include <epoxy/gl.h>
#endif
namespace bts {
// Owner-context submission and zero-wait polling. Keep the established GL
// fence/read ordering; Metal polls the event encoded by sync_to_host instead.
class GpuReadbackFence {
#ifdef __APPLE__
 bool pending_ = false;
#else
 GLsync fence_ = nullptr;
#endif
 public:
 GpuReadbackFence() = default;
 GpuReadbackFence(const GpuReadbackFence &) = delete;
 GpuReadbackFence &operator=(const GpuReadbackFence &) = delete;
 ~GpuReadbackFence(){reset();}
 explicit operator bool() const {
#ifdef __APPLE__
  return pending_;
#else
  return fence_ != nullptr;
#endif
 }
 void reset(){
#ifdef __APPLE__
  pending_ = false;
#else
  if(fence_)glDeleteSync(fence_);
  fence_=nullptr;
#endif
 }
 void submit(blender::gpu::StorageBuf *buffer){
  if(*this)throw std::logic_error("GPU readback already in flight");
  blender::GPU_storagebuf_sync_to_host(buffer);
#ifdef __APPLE__
  pending_=true;
#else
  fence_=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
  if(!fence_)throw std::runtime_error("GPU readback fence allocation failed");
  glFlush();
#endif
 }
 bool read(blender::gpu::StorageBuf *buffer,void *data,bool discard=false){
  if(!*this)return false;
#ifdef __APPLE__
  (void)discard;
  if(!blender::GPU_storagebuf_read_if_ready(buffer,data))return false;
#else
  const auto state=glClientWaitSync(fence_,0,0);
  if(state==GL_WAIT_FAILED)throw std::runtime_error("GPU readback fence poll failed");
  if(state!=GL_ALREADY_SIGNALED&&state!=GL_CONDITION_SATISFIED)return false;
  if(!discard)blender::GPU_storagebuf_read(buffer,data);
#endif
  reset();return true;
 }
};
}
