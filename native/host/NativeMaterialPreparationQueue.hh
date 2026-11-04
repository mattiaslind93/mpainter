#pragma once
#include "NativeMaterialGpuPreparation.hh"
#include "NativeMaterialPreparationStatus.hh"
#include <deque>
#include <map>
namespace bts {
// GPU-owner queue. One bounded step per call; requests for the same resource
// replace pending work while preserving the currently published destination.
class NativeMaterialPreparationQueue {
 public:
  explicit NativeMaterialPreparationQueue(NativeMaterialGpuLibrary &library):library_(library){}
  void request(std::string resource,std::string hash,std::shared_ptr<NativeMaterialGraph> graph){
    if(resource.empty()||hash.empty()||!graph)throw std::invalid_argument("Invalid native preparation request");
    const auto found=jobs_.find(resource);if(found!=jobs_.end()&&found->second.hash==hash&&(found->second.state.pending||(found->second.state.ready&&(!graph->generate_resource_preview||!found->second.state.generated_preview.isNull()))))return;
    std::erase(order_,resource);jobs_.erase(resource);Job job;job.hash=std::move(hash);job.prepare=std::make_unique<NativeMaterialGpuPreparation>(library_,resource,job.hash,std::move(graph));job.state.pending=true;job.state.message=job.prepare->status();jobs_.emplace(resource,std::move(job));order_.push_back(std::move(resource));
  }
  void step(){
    if(order_.empty())return;const auto resource=order_.front();order_.pop_front();auto &job=jobs_.at(resource);
    try{const bool ready=job.prepare->step();job.state.message=job.prepare->status();job.state.maximum_step_ms=job.prepare->maximum_step_ms();if(ready){job.state.pending=false;job.state.ready=true;job.state.generated_preview=job.prepare->generated_preview();job.prepare.reset();}else order_.push_back(resource);}
    catch(const std::exception &error){job.state.pending=false;job.state.error=QString::fromUtf8(error.what());job.state.message="Material preparation failed";job.prepare.reset();}
  }
  void publish_preview(const std::string &resource,const std::string &hash){const auto found=jobs_.find(resource);if(found==jobs_.end()||!found->second.state.ready)throw std::logic_error("Native preview publication requires a ready source");library_.register_content_alias(resource,hash);found->second.hash=hash;}
  void cancel(const std::string &resource){std::erase(order_,resource);jobs_.erase(resource);}
  bool pending()const{return !order_.empty();}
  NativeMaterialPreparationStatus status(const std::string &resource)const{const auto found=jobs_.find(resource);return found==jobs_.end()?NativeMaterialPreparationStatus{}:found->second.state;}
  void clear(){order_.clear();jobs_.clear();}
 private:
  struct Job {std::string hash;std::unique_ptr<NativeMaterialGpuPreparation> prepare;NativeMaterialPreparationStatus state;};
  NativeMaterialGpuLibrary &library_;std::map<std::string,Job> jobs_;std::deque<std::string> order_;
};
}
