// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialSourcePolicy.hh"
#include <utility>
namespace bts {
class NativeFragmentImagePending : public NativeFragmentContextError {
 public:
  explicit NativeFragmentImagePending(const std::string &message)
      : NativeFragmentContextError(NativeFragmentContextCode::Images, message)
  {
  }
};
// The actual library upload route shares this state with CPU callback fixtures.
class NativeFragmentImageReadiness {
  bool ready_ = false;

 public:
  bool ready() const
  {
    return ready_;
  }
  void reset()
  {
    ready_ = false;
  }
  template<class Validate> bool validate_available(const Validate &check)
  {
    try {
      check();
      ready_ = true;
      return true;
    }
    catch (const NativeFragmentImagePending &) {
      ready_ = false;
      return false;
    }
  }
  template<class Advance> bool step(bool required, Advance &&advance)
  {
    if (!required)
      return true;
    if (!ready_)
      ready_ = std::forward<Advance>(advance)();
    return ready_;
  }
  void require_ready(bool required) const
  {
    if (required && !ready_)
      throw NativeFragmentImagePending(
          "Native fragment image upload is pending; target is unchanged");
  }
};
}  // namespace bts
