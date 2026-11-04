// Give the exact included implementation its own symbol. It cannot accidentally
// resolve to a weak inline copy from another host object in either executable.
#define layer_edit_channels native_artist_exact_diff_implementation
#include "LayerEditDiff.hh"
#undef layer_edit_channels
#include <cstdio>
namespace bts {
CompositeChannels native_artist_edit_diff(const TextureSet &before,const TextureSet &after,const QJsonObject &old_state,const QJsonObject &new_state){
 const auto result=native_artist_exact_diff_implementation(before,after,old_state,new_state);unsigned mask=0;for(int c=0;c<17;++c)if(result[c])mask|=1u<<c;std::printf("BTS_NATIVE_ARTIST_DIFF mask=%u\n",mask);return result;
}
}
