#pragma once
#include "MeshMaps.hh"
#include "SceneSnapshot.hh"
#include "FillProjection.hh"
#include "DisplacementBake.hh"
#include <atomic>
#include <functional>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <set>
namespace bts {
struct MeshBakeSettings {
  int resolution=512,height=0,dilation=32,rays=64,sampling=1;
  bool dimensions_locked=true,diffusion=false;
  int rows() const { return height?height:resolution; }
  bool use_project_mesh=false,external_source=false;
  std::map<std::uint32_t,std::set<std::uint32_t>> matched_objects;
  std::set<std::uint32_t> ignore_backface_objects;
  std::array<bool,7> enabled{true,true,true,true,true,true,true};
  // Empty unless a texture set has viewport displacement. Strength 0 leaves the bake unchanged.
  std::map<std::uint32_t,BakeDisplacement> displacement;
  std::function<void(const std::function<void()> &)> with_gpu;
};
struct MeshBakeInput {
  std::uint32_t material_uid=0;
  std::string name;
  MaterialValues source_material;
  std::shared_ptr<const FillSurfaceAtlas> atlas;
};
struct MeshBakeResult {std::uint32_t material_uid=0;MeshMaps maps;};
class MeshBakeJob {
 public:
  MeshBakeJob(SceneSnapshot,std::vector<MeshBakeInput>,MeshBakeSettings);
  MeshBakeJob(std::shared_ptr<const SceneSnapshot>,std::vector<MeshBakeInput>,MeshBakeSettings);
  ~MeshBakeJob();
  void cancel(){{std::lock_guard lock(gpu_mutex_);cancelled.store(true);}gpu_ready_.notify_all();}
  // Called only by the GPU/context owner while the worker waits for its current map.
  void process_gpu();
  bool gpu_pending(){std::lock_guard lock(gpu_mutex_);return bool(gpu_work_);}
  std::atomic_bool cpu_fallback{false};
  std::atomic<int> gpu_maps{0},cpu_ray_maps{0};
  std::atomic<bool> cancelled{false},finished{false};
  std::atomic<int> completed{0},current{0},rows{0};
  std::vector<std::string> tasks;
  std::vector<MeshBakeResult> result;
  std::string error;
 private:
  bool request_gpu(const std::function<void()> &,const std::function<void(const std::function<void()> &)> &,std::string &);
  std::mutex gpu_mutex_;
  std::condition_variable gpu_ready_;
  std::function<void()> gpu_work_;
  bool gpu_running_=false,gpu_complete_=false;
  std::jthread worker_;
};
}
