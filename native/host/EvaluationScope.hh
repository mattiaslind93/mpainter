#pragma once
namespace bts {
// Edit comparison describes metadata and shared immutable storage, never saved checksums.
inline thread_local bool edit_storage_identity=false;
class EditStateScope final {
  bool previous_=edit_storage_identity;
 public:
  EditStateScope(){edit_storage_identity=true;}
  ~EditStateScope(){edit_storage_identity=previous_;}
};
// Low-resolution presentation only; authored pixels and final area filtering stay unchanged.
inline thread_local bool fast_preview_sampling=false;
class PreviewSamplingScope final {
  bool previous_=fast_preview_sampling;
 public:
  explicit PreviewSamplingScope(bool enabled=true){fast_preview_sampling=previous_||enabled;}
  ~PreviewSamplingScope(){fast_preview_sampling=previous_;}
};
}
