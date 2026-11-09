// SPDX-License-Identifier: GPL-3.0-or-later
#include "MaterialLayerParameterImportJob.hh"
#include "FillTextureIO.hh"
#include "MaterialLayeringSourceSnapshots.hh"
#include <map>
#include <stdexcept>

namespace bts {
namespace {
MaterialParameterImportCandidate read_candidate(
    MaterialParameterImportTarget target,
    TextureDocument document,
    const QString &directory,
    const OwnerWorkerJob<MaterialParameterImportCandidate>::Cancel &cancel,
    const OwnerWorkerJob<MaterialParameterImportCandidate>::Progress &report)
{
  if (document.id() != target.project || document.set(target.uid).id != target.set ||
      document.edit_state() != target.signature)
    throw std::runtime_error("Import target differs from the admitted document snapshot");
  report("Reading and verifying parameter package…");
  // The shared reader retains canonical v1/v2 and original payload checks.
  // Reconstruction requires this native capability before any sequence decode,
  // then checks each hydrated member's actual linear processor/input space.
  const auto parameters = read_material_layer_parameter_package(directory, *cancel);
  MaterialParameterImportCandidate result;
  result.target = target;
  bool admitted = false;
  const auto gate = [&](const TextureSet &set) {
    if (cancel->load())
      throw std::runtime_error("Parameter import cancelled");
    if (&set != &document.set(target.uid) || set.id != target.set ||
        set.material_session_uid != target.uid)
      throw std::runtime_error("Detached import target changed before decoding");
    admitted = true;  // The actual owner/native gate ran before worker dispatch.
  };
  std::map<QUuid, std::string> names;
  const auto asset = [](const MaterialParameterOriginal &original, const std::string &name) {
    StudioAsset value;
    value.id = original.asset;
    value.name = QString::fromStdString(name);
    value.filename = original.files.front().descriptor.filename;
    value.path = original.files.front().descriptor.path;
    value.scope = AssetScope::Project;
    value.usages = {"texture"};
    for (const auto &file : original.files)
      value.texture_files.push_back(file.descriptor);
    return value;
  };
  const auto decode =
      [&](const FillTexture &source, const MaterialParameterOriginal &original, bool mask) {
        if (!admitted)
          throw std::logic_error("Parameter decoder ran before target admission");
        const auto [found, inserted] = names.emplace(original.asset, source.name);
        if (!inserted && found->second != source.name)
          throw std::invalid_argument("One parameter original has conflicting source names");
        TextureDecodeControl control;
        control.cancel = cancel.get();
        control.progress = [&](const TextureDecodeProgress &progress) {
          report(QString("%1 — %2 (%3/%4)")
                     .arg(progress.filename, progress.phase)
                     .arg(progress.index + 1)
                     .arg(progress.total));
        };
        return decode_fill_texture(
            asset(original, source.name), mask || source.raw, source.source_space, control);
      };
  const auto processor = [](const FillTexture &source) {
    return QString::fromStdString(material_layer_source_color_transform(source).processor);
  };
  const auto sequence = [&](const FillTexture &source) {
    if (cancel->load())
      throw std::runtime_error("Parameter import cancelled");
    material_layer_color_source_transforms(source);
  };
  result.document = std::make_unique<TextureDocument>(
      reconstruct_material_layer_document(document,
                                          target.uid,
                                          parameters,
                                          target.working_space,
                                          target.scene_linear_to_xyz,
                                          decode,
                                          processor,
                                          gate,
                                          sequence));
  AssetCatalog preflight(QString{});
  for (const auto &original : parameters.originals) {
    auto value = asset(original, names.at(original.asset));
    preflight.add(value);
    result.originals.push_back(std::move(value));
  }
  if (cancel->load())
    throw std::runtime_error("Parameter import cancelled");
  return result;
}
}  // namespace
MaterialLayerParameterImportJob::MaterialLayerParameterImportJob(
    MaterialParameterImportHooks hooks, QObject *parent)
    : QObject(parent), hooks_(std::move(hooks)), reader_(nullptr)
{
  timer_.setInterval(16);
  connect(&timer_, &QTimer::timeout, this, [this] { poll(); });
}
MaterialLayerParameterImportJob::~MaterialLayerParameterImportJob()
{
  shutdown();
}
void MaterialLayerParameterImportJob::start(MaterialParameterImportTarget target,
                                            TextureDocument document,
                                            const QString &directory)
{
  if (active_ || reader_.pending() || terminal_)
    throw std::logic_error("A parameter import is still finishing");
  if (!hooks_.current || !hooks_.validate || !hooks_.claim || !hooks_.release || !hooks_.publish)
    throw std::logic_error("Parameter import is unavailable");
  if (!hooks_.current(target))
    throw std::runtime_error("The parameter import target changed");
  hooks_.validate(target);  // Owner/native admission BEFORE any package decode.
  if (document.id() != target.project || document.set(target.uid).id != target.set ||
      document.edit_state() != target.signature)
    throw std::runtime_error("The parameter import snapshot changed");

  target_ = std::move(target);
  token_ = QUuid::createUuid();
  cancelled_ = false;
  error_.clear();
  text_ = "Reading parameter package…";
  active_ = true;
  try {
    reader_.start(
        [target = target_, document = std::move(document), directory](auto cancel,
                                                                      auto report) mutable {
          return read_candidate(std::move(target), std::move(document), directory, cancel, report);
        },
        [this] { return active_ && !cancelled_ && hooks_.current(target_); },
        [this](MaterialParameterImportCandidate result) {
          candidate_ = std::move(result);
          text_ = "Waiting to prepare material images…";
        },
        [this](const QString &error) { fail(error); },
        [this](const QString &text) {
          if (active_ && !cancelled_)
            text_ = text;
        },
        [this] {
          if (active_ && (cancelled_ || !candidate_))
            finish();
        });  // NO deliver_when.
    timer_.start();
  }
  catch (...) {
    active_ = false;
    token_ = QUuid();
    throw;
  }
}
void MaterialLayerParameterImportJob::release()
{
  if (leased_) {
    hooks_.release(token_);
    leased_ = false;
  }
}
void MaterialLayerParameterImportJob::cancel()
{
  if (!active_)
    return;
  cancelled_ = true;
  text_ = "Cancelling parameter import…";
  reader_.cancel();
  release();
  candidate_.reset();
  if (!reader_.pending())
    finish();
}
void MaterialLayerParameterImportJob::fail(const QString &error)
{
  error_ = error;
  cancel();
}
void MaterialLayerParameterImportJob::finish()
{
  release();
  candidate_.reset();
  target_ = {};
  timer_.stop();
  active_ = false;
  token_ = QUuid();
  text_ = !error_.isEmpty() ? "Parameter import failed" :
          cancelled_        ? "Parameter import cancelled; project unchanged." :
                              "Material parameters imported.";
}
void MaterialLayerParameterImportJob::poll()
{
  if (!active_) {
    timer_.stop();
    return;
  }
  try {
    if (!cancelled_ && !hooks_.current(target_)) {
      cancel();
      return;
    }
    reader_.poll();
    if (!active_ || reader_.pending())
      return;
    if (cancelled_) {
      finish();
      return;
    }
    if (!candidate_)
      return;
    hooks_.validate(target_);
    if (!leased_) {
      if (hooks_.busy && hooks_.busy())
        return;
      if (!hooks_.claim(token_))
        return;
      leased_ = true;
    }
    text_ = "Preparing material images…";
    if (hooks_.publish(*candidate_, token_))
      finish();
  }
  catch (const std::exception &error) {
    fail(QString::fromUtf8(error.what()));
  }
}
void MaterialLayerParameterImportJob::shutdown() noexcept
{
  if (terminal_)
    return;
  terminal_ = true;
  try {
    cancel();
    reader_.shutdown();
    release();
    candidate_.reset();
    active_ = false;
    timer_.stop();
  }
  catch (...) {
    qFatal("Parameter import owner cleanup failed before Host shutdown");
  }
}
}  // namespace bts
