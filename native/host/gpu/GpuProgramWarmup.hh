// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <exception>
#include <stdexcept>
#include <utility>

namespace bts {
// Fixed program-set admission/drain state; no threads, scheduling or GPU ownership.
// Owners retain each submitted handle immediately, including partial submission.
class GpuProgramWarmup {
 public:
  enum class State { Pending, Ready, Failed };
  explicit GpuProgramWarmup(int programs = 2) : programs_(programs)
  {
    if (programs <= 0)
      throw std::invalid_argument("A GPU program set cannot be empty");
  }
  template<class Submit, class Poll, class Ready, class Pending>
  State prepare(Submit submit, Poll poll, Ready ready, Pending pending)
  {
    drain(poll);
    if (!started_) {
      started_ = true;
      try {
        for (int index = 0; index < programs_; ++index)
          submit(index);
      }
      catch (const std::exception &error) {
        fail(std::string("GPU stack program submission failed: ") + error.what());
      }
      catch (...) {
        fail("GPU stack program submission failed with an unknown exception");
      }
      drain(poll);
    }
    // A failed pair remains Pending only to drain a submitted sibling; it can
    // never become Ready, even if that sibling eventually compiles successfully.
    for (int index = 0; index < programs_; ++index)
      if (pending(index))
        return State::Pending;
    if (!failure_.empty())
      return State::Failed;
    for (int index = 0; index < programs_; ++index)
      if (!ready(index)) {
        fail("GPU stack program submission did not produce every shader");
        return State::Failed;
      }
    return State::Ready;
  }
  template<class Poll> void drain(Poll poll)
  {
    // Always visit both siblings, including after a previous failure.
    for (int index = 0; index < programs_; ++index)
      if (!poll(index))
        fail(programs_ == 2 ?
                 (index == 0 ? "GPU stack RGBA shader compilation failed" :
                               "GPU stack scalar mask shader compilation failed") :
                 "GPU shader program " + std::to_string(index) + " compilation failed");
  }
  std::string take_error()
  {
    if (delivered_ || failure_.empty())
      return {};
    auto result = failure_;
    delivered_ = true;
    return result;
  }

 private:
  void fail(std::string message)
  {
    if (failure_.empty())
      failure_ = std::move(message);
  }
  bool started_ = false, delivered_ = false;
  int programs_;
  std::string failure_;
};
}  // namespace bts
