// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtCore>
#include "NativeMaterialHeightUi.hh"
#include "BlenderHost.hh"
#include "ResourceIO.hh"
namespace bts {
NativeMaterialHeightUi::NativeMaterialHeightUi(BlenderHost &host,
                                               NativeHeightUiHooks hooks,
                                               QObject *parent)
    : QObject(parent), host_(host), hooks_(std::move(hooks)), worker_(this)
{
  timer_.setInterval(16);
  connect(&timer_, &QTimer::timeout, this, [this] { poll(); });
}
NativeMaterialHeightUi::~NativeMaterialHeightUi()
{
  shutdown();
}
void NativeMaterialHeightUi::request(NativeHeightUiTarget target, NativeHeightPolicy policy)
{
  validate_native_height_policy(policy);
  if (terminal_)
    throw std::logic_error("Native Height owner has shut down");
  if (active_)
    throw std::logic_error("Another native Height request is still finishing");
  if (!target.current || !target.current())
    throw std::invalid_argument("Native Height target changed before preparation");
  target_ = std::move(target);
  policy_ = policy;
  active_ = true;
  cancelled_ = false;
  if (policy == NativeHeightPolicy::None) {
    if (hooks_.progress)
      hooks_.progress("Updating authored Height interpretation…");
    timer_.start();
    return;
  }
  try {
    if (hooks_.progress)
      hooks_.progress("Validating original authored Height on a worker…");
    const auto asset = target_.source;
    const auto resources = target_.resources;
    worker_.start(
        [asset, resources](auto cancel, auto) {
          return std::make_unique<NativeMaterialHeightResource>(
              prepare_native_material_height_resource(asset, resources, *cancel));
        },
        [this] { return !cancelled_ && target_.current && target_.current(); },
        [this](auto resource) {
          resource_ = std::move(resource);
          timer_.start();
        },
        [this](const auto &message) { fail(message); },
        {},
        [this] {
          if (active_ && !resource_ && !gpu_)
            finish();
        });
  }
  catch (...) {
    finish();
    throw;
  }
}
void NativeMaterialHeightUi::cancel()
{
  cancelled_ = true;
  worker_.cancel();
  if (resource_ || gpu_ || policy_ == NativeHeightPolicy::None)
    timer_.start();
}
void NativeMaterialHeightUi::poll()
{
  if (!active_ || !host_.project_gpu_available())
    return;
  try {
    if (cancelled_) {
      finish();
      return;
    }
    if (!target_.current || !target_.current())
      throw std::runtime_error(
          "Native Height target changed during preparation; result discarded");
    if (policy_ != NativeHeightPolicy::None && !gpu_) {
      if (!resource_)
        return;
      if (hooks_.progress)
        hooks_.progress("Preparing isolated native Height GPU program…");
      gpu_ = host_.stage_native_height_gpu(target_.source.id, std::move(*resource_));
      resource_.reset();
      return;
    }
    if (gpu_ && !gpu_->ready()) {
      if (hooks_.progress)
        hooks_.progress("Uploading isolated native Height images / preparing GPU program…");
      host_.step_native_height_gpu(*gpu_);
      return;
    }
    // A separate owner tick after compilation rechecks cancellation, document,
    // selection, source and pending property gestures before any publication.
    if (hooks_.publish)
      hooks_.publish(gpu_.get(), policy_, target_);
    finish();
  }
  catch (const std::exception &error) {
    fail(QString::fromUtf8(error.what()));
  }
  catch (...) {
    fail("Native Height preparation failed with an unknown native error");
  }
}
void NativeMaterialHeightUi::finish()
{
  timer_.stop();
  gpu_.reset();
  resource_.reset();
  target_ = {};
  active_ = false;
  if (hooks_.finished)
    hooks_.finished();
}
void NativeMaterialHeightUi::fail(const QString &message)
{
  const bool notify = !cancelled_;
  finish();
  if (notify && hooks_.error)
    hooks_.error(message);
}
void NativeMaterialHeightUi::shutdown() noexcept
{
  terminal_ = true;
  cancelled_ = true;
  timer_.stop();
  worker_.shutdown();
  gpu_.reset();
  resource_.reset();
  target_ = {};
  active_ = false;
  hooks_ = {};
}
}  // namespace bts
