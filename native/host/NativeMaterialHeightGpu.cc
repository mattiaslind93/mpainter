#include <QtCore>
#include "NativeMaterialHeightGpuPrivate.hh"
#include "BlenderHost.hh"
#include <cstdio>
#include <exception>
namespace bts {
NativeMaterialHeightGpu::NativeMaterialHeightGpu(BlenderHost &host):host_(&host),impl_(std::make_unique<Impl>()){}
NativeMaterialHeightGpu::~NativeMaterialHeightGpu(){if(host_)try{host_->discard_native_height_gpu(*this);}catch(...){std::fputs("BTS_NATIVE_HEIGHT_OWNER_CLEANUP_FAILED\n",stderr);std::terminate();}}
void NativeMaterialHeightGpu::commit()noexcept{if(!impl_||!impl_->change)std::terminate();impl_->change->commit();}
}
