// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace bts {
// One copied New request, owned by its UI context. This holds no native IDs,
// executes no work and never queues multiple commands behind compilation.
template<class Options> class PendingProjectCreate {
 public:
  struct Request {
    Options options;
    std::string project;
    std::uint64_t revision, generation;
  };
  void replace(Options options, std::string project, std::uint64_t revision)
  {
    if (retired_)
      return;
    ++generation_;
    request_ = Request{std::move(options), std::move(project), revision, generation_};
  }
  std::uint64_t generation() const
  {
    return generation_;
  }
  void cancel()
  {
    ++generation_;
    request_.reset();
  }
  bool cancel_generation(std::uint64_t generation)
  {
    if (generation != generation_)
      return false;
    cancel();
    return true;
  }
  // Save can pump events and restart compilation. Restore only this generation,
  // never overwrite a newer queued request or revive a cancelled one.
  bool restore(Request request)
  {
    if (retired_ || request.generation != generation_ || request_)
      return false;
    request_ = std::move(request);
    return true;
  }
  void retire()
  {
    retired_ = true;
    cancel();
  }
  bool pending() const
  {
    return request_.has_value();
  }
  bool current(const Request &request, std::string_view project, std::uint64_t revision) const
  {
    return !retired_ && request.generation == generation_ && request.project == project &&
           request.revision == revision;
  }
  std::optional<Request> take_ready(std::uint64_t expected_generation,
                                    bool blocked,
                                    std::string_view project,
                                    std::uint64_t revision)
  {
    if (expected_generation != generation_)
      return {};
    if (request_ && !current(*request_, project, revision))
      cancel();
    if (blocked || !request_)
      return {};
    auto result = std::move(request_);
    request_.reset();
    return result;
  }

 private:
  std::optional<Request> request_;
  std::uint64_t generation_ = 0;
  bool retired_ = false;
};
}  // namespace bts
