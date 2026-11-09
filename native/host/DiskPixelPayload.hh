#pragma once
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <memory>
#include <mutex>
#include <map>
#include <vector>
#include <stdexcept>
#if defined(Q_OS_UNIX)
#  include <unistd.h>
#endif
namespace bts {
// Immutable decoded pixels share one disk snapshot across document/history copies.
// Eviction is an idle-owner operation, after consumers have stopped reading.
template<class Pixel> class DiskPixelPayload {
  struct State {
    mutable std::mutex mutex;
    mutable std::shared_ptr<const std::vector<Pixel>> pixels;
    mutable std::unique_ptr<QTemporaryFile> file;
    std::size_t count;
    explicit State(std::shared_ptr<const std::vector<Pixel>> p)
        : pixels(std::move(p)), count(pixels->size())
    {
    }
    std::shared_ptr<const std::vector<Pixel>> read() const
    {
      std::lock_guard lock(mutex);
      if (!pixels) {
        auto restored = std::make_shared<std::vector<Pixel>>(count);
        if (!file || !file->seek(0))
          throw std::runtime_error("Texture cache is unavailable");
        const qint64 bytes = qint64(count * sizeof(Pixel));
        char *data = reinterpret_cast<char *>(restored->data());
        for (qint64 offset = 0; offset < bytes;) {
          const auto n = file->read(data + offset,
                                    std::min<qint64>(bytes - offset, 4 * 1024 * 1024));
          if (n <= 0)
            throw std::runtime_error("Cannot read texture cache");
          offset += n;
        }
        pixels = std::move(restored);
      }
      return pixels;
    }
    std::size_t spill() const
    {
      std::lock_guard lock(mutex);
      if (!pixels)
        return 0;
      if (!file) {
        const auto folder = qEnvironmentVariable(
                                "BTS_RECOVERY_DIR",
                                QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                                    "/history") +
                            "/unloaded-textures";
        if (!QDir().mkpath(folder))
          throw std::runtime_error("Cannot create texture cache");
        auto candidate = std::make_unique<QTemporaryFile>(folder + "/texture-XXXXXX.raw");
        if (!candidate->open())
          throw std::runtime_error("Cannot open texture cache");
        const auto bytes = qint64(count * sizeof(Pixel));
        const char *data = reinterpret_cast<const char *>(pixels->data());
        for (qint64 offset = 0; offset < bytes;) {
          const auto n = candidate->write(data + offset,
                                          std::min<qint64>(bytes - offset, 4 * 1024 * 1024));
          if (n <= 0)
            throw std::runtime_error("Cannot write texture cache; pixels retained");
          offset += n;
        }
        if (!candidate->flush())
          throw std::runtime_error("Cannot flush texture cache; pixels retained");
#if defined(Q_OS_UNIX)
        if (fsync(candidate->handle()))
          throw std::runtime_error("Cannot sync texture cache; pixels retained");
#endif
        file = std::move(candidate);
      }
      const auto bytes = pixels.use_count() == 1 ? count * sizeof(Pixel) : 0;
      pixels.reset();
      return bytes;
    }
  };
  std::shared_ptr<State> state_;
  void assign(std::shared_ptr<const std::vector<Pixel>> pixels)
  {
    if (!pixels) {
      state_.reset();
      return;
    }
    static std::mutex mutex;
    static std::map<const void *, std::weak_ptr<State>> shared;
    std::lock_guard lock(mutex);
    const auto key = pixels.get();
    if (auto found = shared[key].lock()) {
      std::lock_guard owner(found->mutex);
      if (found->pixels == pixels) {
        state_ = std::move(found);
        return;
      }
    }
    state_ = std::make_shared<State>(std::move(pixels));
    shared[key] = state_;
    if (shared.size() > 4096)
      std::erase_if(shared, [](const auto &entry) { return entry.second.expired(); });
  }

 public:
  DiskPixelPayload() = default;
  DiskPixelPayload(std::shared_ptr<const std::vector<Pixel>> p)
  {
    assign(std::move(p));
  }
  DiskPixelPayload &operator=(std::shared_ptr<const std::vector<Pixel>> p)
  {
    assign(std::move(p));
    return *this;
  }
  explicit operator bool() const
  {
    return bool(state_);
  }
  operator std::shared_ptr<const std::vector<Pixel>>() const
  {
    return state_ ? state_->read() : nullptr;
  }
  operator std::weak_ptr<const std::vector<Pixel>>() const
  {
    return std::shared_ptr<const std::vector<Pixel>>(*this);
  }
  const std::vector<Pixel> *get() const
  {
    return state_ ? state_->read().get() : nullptr;
  }
  const std::vector<Pixel> *operator->() const
  {
    return get();
  }
  const std::vector<Pixel> &operator*() const
  {
    return *get();
  }
  void reset()
  {
    state_.reset();
  }
  std::size_t spill() const
  {
    return state_ ? state_->spill() : 0;
  }
  std::size_t resident_bytes() const
  {
    if (!state_)
      return 0;
    std::lock_guard lock(state_->mutex);
    return state_->pixels ? state_->count * sizeof(Pixel) : 0;
  }
  std::size_t size() const
  {
    return state_ ? state_->count : 0;
  }
  const void *identity() const
  {
    return state_.get();
  }
  friend bool operator==(const DiskPixelPayload &a, const DiskPixelPayload &b)
  {
    return a.state_ == b.state_;
  }
};
}  // namespace bts
