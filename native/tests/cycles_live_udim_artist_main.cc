// Production main SOURCE is unchanged. Only its optional demo call is
// augmented with the actual owner and a fixture-only edit callback.
#include "CyclesLiveUdimArtist.hh"
#include "../harness/GuiFixDemo.hh"
#define main cycles_udim_production_main
#define start_gui_fix_demo(...) start_cycles_udim_artist_demo(__VA_ARGS__, &host, [&](const std::function<void(bts::TextureSet &)> &fn){edit_material(fn);}, [&](const std::function<void()> &hook){const auto original=viewport->frame_done;viewport->frame_done=[original,hook](double ms){if(original)original(ms);hook();};})
#include "../host/app/main.cc"
#undef start_gui_fix_demo
#undef main
int main(int argc,char **argv){return cycles_udim_production_main(argc,argv);}
