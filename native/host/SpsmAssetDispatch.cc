#include "SpsmAssetDispatch.hh"
#include <mutex>
#include <stdexcept>

namespace bts {
namespace {
std::mutex hydrator_mutex;
SpsmAssetHydrator hydrator;
}
void set_spsm_asset_hydrator(SpsmAssetHydrator value)
{
  const std::lock_guard lock(hydrator_mutex);
  hydrator = std::move(value);
}
bool hydrate_spsm_asset(StudioAsset &asset)
{
  if (!is_spsm_archive_asset(asset)) throw std::invalid_argument("Not an SPSM material or SPMSK mask");
  SpsmAssetHydrator prepare;
  {
    const std::lock_guard lock(hydrator_mutex);
    prepare = hydrator;
  }
  if (!prepare) throw std::runtime_error("Direct SPSM import is not initialized");
  // Never hold the registry lock while doing IO, parsing or color management.
  if (!prepare(asset)) return false;
  if(is_spmsk_asset(asset)){if(!asset.smart_mask)throw std::logic_error("SPMSK preparation returned no native mask");asset.smart_material=false;}
  else{if (!asset.material_preset) throw std::logic_error("SPSM preparation returned no native preset");asset.smart_material = true;}
  return true;
}
}
