#pragma once
#include "BlendAssetLibrary.hh"
#include "NativeMaterialPreview.hh"
#include <future>
class QWidget;
namespace bts {
struct BlendAssetBrowserHooks {
 bool native_shader=false; // Saved previews/placeholders only; no graph preparation while browsing.
 std::function<std::shared_future<BlendAssetLibrary>(const QString &,std::shared_ptr<std::stop_source>,std::function<void(const QString &)>)> scan;
 std::function<std::shared_future<PreparedBlendMaterial>(const QString &,const QString &,std::shared_ptr<std::atomic_bool>,bool preview_only)> prepare;
 std::function<void(PreparedBlendMaterial)> install;
 std::function<std::shared_future<NativeMaterialPreviewResult>(const QString &,const QString &,std::shared_ptr<std::atomic_bool>)> base_color_preview;
};
void browse_blend_asset_library(QWidget *,BlendAssetBrowserHooks);
}
