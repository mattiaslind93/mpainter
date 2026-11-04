#pragma once
#include "NativeMaterialImagePanel.hh"
#include "NativeMaterialHeightPolicy.hh"
namespace bts {
// Passive selection/Undo is not an image edit. Explain the same mutation guard
// locally, clear stale enabled slots, and do not start inspection/preparation.
inline bool restrict_native_image_refresh(NativeMaterialImagePanel &panel,NativeHeightPolicy policy){
 if(policy==NativeHeightPolicy::None)return false;
 try{require_native_height_image_replacement(policy);}
 catch(const std::invalid_argument &error){panel.synchronize({},false);panel.error(QString::fromUtf8(error.what()));return true;}
 return false;
}
}
