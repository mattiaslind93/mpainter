#include "MetalContextOwner.hh"
#include "QtSharedContext.hh"
#include "GHOST_ContextMTL.hh"
#include "GPU_context_hosted.hh"
namespace blender {void BTS_system_gpu_context_callbacks_set(GHOST_IContext *(*)(),void (*)(GHOST_IContext *));}
namespace bts {
namespace {
GHOST_IContext *create(){
 GHOST_GPUSettings settings{};settings.context_type=GHOST_kDrawingContextTypeMetal;
 auto *context=new GHOST_ContextMTL(GHOST_CONTEXT_PARAMS_FROM_GPU_SETTINGS_OFFSCREEN(settings),nil,nil);
 if(context->initializeDrawingContext()!=GHOST_kSuccess){delete context;return nullptr;}
 return context;
}
void dispose(GHOST_IContext *context){delete context;}
}
void install_metal_shared_contexts(){blender::BTS_system_gpu_context_callbacks_set(create,dispose);}
void drain_shared_shader_contexts()noexcept{}
SharedShaderContextStats shared_shader_context_stats(){return {};}
void install_shared_ghost_context(QOpenGLContext *,bool){
 blender::BTS_system_gpu_context_callbacks_set(nullptr,nullptr);
 blender::GPU_context_hosted_callbacks_set(nullptr,nullptr);
}
}
