#pragma once
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
namespace bts::performance {
using Clock = std::chrono::steady_clock;
inline bool enabled()
{
  static const bool value = std::getenv("BTS_PERFORMANCE_PROFILE") != nullptr;
  return value;
}
inline double ms(Clock::time_point start)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
inline double thread_ms()
{
#if defined(__linux__)
  timespec time{};
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time);
  return time.tv_sec * 1000. + time.tv_nsec / 1e6;
#else
  return 1000. * std::clock() / CLOCKS_PER_SEC;
#endif
}
struct Record {
  std::string flow, phase;
  double wall = 0, cpu = 0, queue = 0;
  bool ui = false;
};
inline std::mutex mutex;
inline std::string current_flow;
inline std::vector<Record> records;
inline thread_local std::vector<Record> ui_phases;
inline bool session_logging()
{
  static const bool value = std::getenv("BTS_SESSION_PROFILE") != nullptr;
  return value;
}
inline void start_ui_event()
{
  ui_phases.clear();
}
inline void warn_stall(const std::string &event, double wall, double cpu, int nested)
{
  if (wall <= 100 || (!session_logging() && !enabled()))
    return;
  std::string phases;
  for (const auto &r : ui_phases)
    if (r.wall >= 1) {
      char value[96];
      std::snprintf(value, sizeof(value), "=%.2fms ", r.wall);
      phases += r.phase + value;
    }
  std::fprintf(stderr,
               "FRYSVARNING ui_event=%s wall_ms=%.2f cpu_ms=%.2f nested_events=%d phases=[%s]\n",
               event.c_str(),
               wall,
               cpu,
               nested,
               phases.c_str());
}
inline std::string flow()
{
  if (!enabled())
    return {};
  std::lock_guard lock(mutex);
  return current_flow;
}
inline void begin(std::string label)
{
  if (enabled()) {
    std::lock_guard lock(mutex);
    current_flow = std::move(label);
  }
}
inline void record(Record value)
{
  if (enabled()) {
    // Tiny page dispatches must not hide the enclosing slow upload/draw phase.
    if (value.ui && value.wall >= 1) {
      if (ui_phases.size() < 48)
        ui_phases.push_back(value);
      else {
        auto smallest = std::min_element(
            ui_phases.begin(), ui_phases.end(), [](const auto &a, const auto &b) {
              return a.wall < b.wall;
            });
        if (smallest->wall < value.wall)
          *smallest = value;
      }
    }
    if (session_logging()) {
      if (value.wall >= 1 || value.queue >= 1)
        std::fprintf(
            stderr,
            "BTS_PERFORMANCE_PROFILE flow=%s phase=%s wall_ms=%.3f cpu_ms=%.3f queue_ms=%.3f ui=%d\n",
            value.flow.c_str(),
            value.phase.c_str(),
            value.wall,
            value.cpu,
            value.queue,
            int(value.ui));
    }
    else {
      std::lock_guard lock(mutex);
      if (records.size() < 100000)
        records.push_back(std::move(value));
    }
  }
}
struct Scope {
  std::string label, owner;
  Clock::time_point start{};
  double cpu = 0;
  bool active = false;
  explicit Scope(const char *phase) : label(phase), owner(flow()), active(enabled())
  {
    if (active) {
      start = Clock::now();
      cpu = thread_ms();
    }
  }
  ~Scope()
  {
    if (active)
      record({owner,
              label,
              ms(start),
              thread_ms() - cpu,
              0,
              QCoreApplication::instance() &&
                  QThread::currentThread() == QCoreApplication::instance()->thread()});
  }
};
inline QJsonArray snapshot()
{
  QJsonArray out;
  std::lock_guard lock(mutex);
  for (const auto &r : records)
    out.append(QJsonObject{{"flow", QString::fromStdString(r.flow)},
                           {"phase", QString::fromStdString(r.phase)},
                           {"wall_ms", r.wall},
                           {"cpu_ms", r.cpu},
                           {"queue_ms", r.queue},
                           {"ui", r.ui}});
  return out;
}
}  // namespace bts::performance
