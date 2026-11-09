#pragma once
#include "BlendMaterial.hh"
#include <functional>
#include <future>
class QWidget;
namespace bts {
void choose_blend_material(
    QWidget *,
    std::function<std::shared_future<PreparedBlendMaterial>(
        const QString &, const QString &, std::shared_ptr<std::atomic_bool>)>,
    std::function<void(PreparedBlendMaterial)>,
    bool native = false);
}
