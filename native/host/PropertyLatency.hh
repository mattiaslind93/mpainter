// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
namespace bts {
using PropertyClock = std::chrono::steady_clock;
inline bool property_timing_enabled()
{
  static const bool enabled = std::getenv("BTS_PROPERTY_PERFORMANCE") != nullptr;
  return enabled;
}
inline double property_ms(PropertyClock::time_point start)
{
  return std::chrono::duration<double, std::milli>(PropertyClock::now() - start).count();
}
struct PropertyTrace {
  std::uint64_t event = 0;
  std::string label;
  PropertyClock::time_point started{};
  double invalidation_ms = 0, composite_ms = 0, padding_ms = 0, upload_ms = 0, gpu_ms = 0;
  bool preview = false;
};
// Owner-thread instrumentation. Workers receive private trace copies in their request/result.
// A trace measures first presentation once. Later stroke/finalize requests must
// not inherit a previously presented property or report its growing age again.
inline PropertyTrace property_current, property_waiting;
inline PropertyClock::time_point property_frame_start{};
inline void property_paint_begin()
{
  if (property_timing_enabled())
    property_frame_start = PropertyClock::now();
}
inline QJsonArray property_visible_records;
inline std::array<std::uint64_t, 17> property_upload_counts{};
inline bool property_pipeline_pending = false;
inline std::uint64_t property_next_event = 0, property_presented_event = 0;
inline void property_event(const char *label)
{
  if (property_timing_enabled())
    property_current = {++property_next_event, label, PropertyClock::now()};
}
inline void property_reset_metrics()
{
  property_current = {};
  property_waiting = {};
  property_visible_records = {};
  property_upload_counts = {};
  property_presented_event = property_next_event;
}
inline void property_uploaded(const PropertyTrace &trace, double upload_ms)
{
  if (!property_timing_enabled() || !trace.event || trace.event <= property_presented_event)
    return;
  if (trace.event >= property_waiting.event) {
    property_waiting = trace;
    property_waiting.upload_ms += upload_ms;
  }
}
inline void property_upload_channel(int channel)
{
  if (property_timing_enabled())
    ++property_upload_counts.at(channel);
}
inline void property_frame_done()
{
  if (!property_timing_enabled() || !property_waiting.event)
    return;
  const auto trace = property_waiting;
  property_waiting = {};
  if (trace.event <= property_presented_event)
    return;
  property_presented_event = trace.event;
  if (property_current.event == trace.event)
    property_current = {};
  const double latency = property_ms(trace.started),
               frame_ms = property_frame_start == PropertyClock::time_point{} ?
                              latency :
                              property_ms(property_frame_start),
               queue_ms = std::max(0., latency - frame_ms);
  QJsonObject record{{"event", qint64(trace.event)},
                     {"property", QString::fromStdString(trace.label)},
                     {"visible_ms", latency},
                     {"pre_frame_ms", queue_ms},
                     {"presentation_frame_ms", frame_ms},
                     {"preview", trace.preview},
                     {"invalidation_ms", trace.invalidation_ms},
                     {"composite_ms", trace.composite_ms},
                     {"padding_ms", trace.padding_ms},
                     {"upload_ms", trace.upload_ms},
                     {"gpu_ms", trace.gpu_ms},
                     {"gpu_submit_cpu_ms", trace.gpu_ms}};
  property_visible_records.append(record);
  if (property_visible_records.size() > 4096)
    property_visible_records.removeFirst();
  std::printf(
      "BTS_EDIT_MS property=%s event=%llu phase=visible visible_ms=%.3f invalidation_ms=%.3f composite_ms=%.3f padding_ms=%.3f upload_ms=%.3f gpu_ms=%.3f preview=%d pre_frame_ms=%.3f presentation_frame_ms=%.3f\n",
      trace.label.c_str(),
      static_cast<unsigned long long>(trace.event),
      latency,
      trace.invalidation_ms,
      trace.composite_ms,
      trace.padding_ms,
      trace.upload_ms,
      trace.gpu_ms,
      int(trace.preview),
      queue_ms,
      frame_ms);
  std::fflush(stdout);
}
inline QJsonObject property_latency_state()
{
  QJsonArray uploads;
  for (auto count : property_upload_counts)
    uploads.append(qint64(count));
  return {{"events", qint64(property_next_event)},
          {"visible", property_visible_records},
          {"upload_counts", uploads},
          {"pending", property_pipeline_pending}};
}
}  // namespace bts
