#pragma once
#include <array>
#include <functional>
#include <memory>
#include <cmath>
#include <cstdint>
#include <utility>
#include <span>
#include <stdexcept>
#include <string>
namespace bts {
class NativeMaterialObjectTable;
// Explicit table fields only: UID bits, random, two full affine matrices and
// color/alpha. No struct padding, texspace/ORCO, mesh or UV data is observed.
class NativeWorldNormalObjectContext {
 std::string bytes_;
 public:
 bool observe(std::span<const std::array<float,4>> rows){
  if(rows.empty()||rows.size()%12)throw std::invalid_argument("Resident Normal object table is missing or malformed");
  std::string bytes;bytes.reserve(rows.size()/12*38*sizeof(float));
  const auto append=[&](float value,bool opaque=false){if(!opaque&&!std::isfinite(value))throw std::invalid_argument("Resident Normal object property is nonfinite");bytes.append(reinterpret_cast<const char *>(&value),sizeof(value));};
  for(std::size_t i=0;i<rows.size();i+=12){append(rows[i][0],true);append(rows[i][1]);for(std::size_t r=i+1;r<i+9;++r)for(float value:rows[r])append(value);for(float value:rows[i+11])append(value);}
  if(bytes==bytes_)return false;
  bytes_=std::move(bytes);return true;
 }
 void clear(){bytes_.clear();}
};
// The production observation state machine is CPU-only. It shares the exact
// typed provider with the owner-mutation test, including failure/recovery.
class NativeWorldNormalObjectObserver {
 NativeWorldNormalObjectContext context_;std::string error_;
 public:
 using Provider=std::function<std::shared_ptr<const NativeMaterialObjectTable>(std::uint32_t)>;
 template<class SourceProvider>bool observe(const SourceProvider &provider,std::uint32_t material){
  try{
   if(!provider)throw std::invalid_argument("Native world Normal object owner is not ready");
   const auto table=provider(material);if(!table)throw std::invalid_argument("Native world Normal object table is unavailable");
   const bool changed=context_.observe(table->rows())||!error_.empty();error_.clear();return changed;
  }catch(const std::exception &error){const bool changed=error_!=error.what();error_=error.what();return changed;}
 }
 template<class SourceProvider>bool notify(bool owner_revision,const SourceProvider &provider,std::uint32_t material,bool geometry_revision=false){
  if(!owner_revision&&!geometry_revision)return false;
  const bool properties_changed=observe(provider,material);
  return properties_changed||geometry_revision;
 }
 const std::string &error()const{return error_;}
 void clear(){context_.clear();error_.clear();}
};
}
