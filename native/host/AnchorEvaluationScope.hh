#pragma once
namespace bts {
struct TextureSet;
// CPU export/oracle isolation must not sever references to lower layers outside
// the selected group. The owner snapshot outlives this non-copyable scope; no
// document pointer escapes into the model, UI, GPU queue or another thread.
class AnchorEvaluationScope {
 inline static thread_local const TextureSet *source_=nullptr;
 const TextureSet *previous_;
 public:
 explicit AnchorEvaluationScope(const TextureSet &source):previous_(source_){source_=&source;}
 ~AnchorEvaluationScope(){source_=previous_;}
 AnchorEvaluationScope(const AnchorEvaluationScope &)=delete;
 AnchorEvaluationScope &operator=(const AnchorEvaluationScope &)=delete;
 static const TextureSet *source(){return source_;}
};
}
