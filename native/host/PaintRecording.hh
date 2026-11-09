#pragma once
#include <cstdint>
#include <cstdio>
namespace bts {
inline thread_local bool record_gpu_paint = false;
inline thread_local bool cpu_paint_evaluation_forbidden = false;
class GpuPersistenceScope {
  bool previous_ = cpu_paint_evaluation_forbidden;

 public:
  GpuPersistenceScope()
  {
    cpu_paint_evaluation_forbidden = true;
  }
  ~GpuPersistenceScope()
  {
    cpu_paint_evaluation_forbidden = previous_;
  }
};
inline thread_local bool paint_metadata_only = false;
inline thread_local std::uint64_t local_paint_materializations = 0;
class PaintRecordingScope {
  bool previous_record_ = record_gpu_paint, previous_metadata_ = paint_metadata_only;
  bool enabled_;
  std::uint64_t started_ = local_paint_materializations;

 public:
  explicit PaintRecordingScope(bool enabled = true) : enabled_(enabled)
  {
    record_gpu_paint = enabled;
    paint_metadata_only = enabled;
  }
  ~PaintRecordingScope()
  {
    if (enabled_ && local_paint_materializations != started_)
      std::fprintf(stderr,
                   "BTS_GPU_AUTHORING_CPU_READ materialized=%llu\n",
                   (unsigned long long)(local_paint_materializations - started_));
    record_gpu_paint = previous_record_;
    paint_metadata_only = previous_metadata_;
  }
};
class PaintMetadataScope {
  bool previous_ = paint_metadata_only;

 public:
  PaintMetadataScope()
  {
    paint_metadata_only = true;
  }
  ~PaintMetadataScope()
  {
    paint_metadata_only = previous_;
  }
};
}  // namespace bts
