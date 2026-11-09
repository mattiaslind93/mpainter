// Test-only entry: production main and renderer remain byte-identical.
#include "CyclesLiveBurst.hh"
#include "../harness/GuiFixDemo.hh"
#define main cycles_burst_production_main
#define start_gui_fix_demo(...) start_cycles_live_burst(__VA_ARGS__, &host, [&](const std::function<void(bts::TextureSet &)> &fn){edit_material(fn);}, [&](const std::function<void()> &hook){const auto original=viewport->frame_done;viewport->frame_done=[original,hook](double ms){if(original)original(ms);hook();};})
#include "../host/main.cc"
#undef start_gui_fix_demo
#undef main
int main(int argc,char **argv){return cycles_burst_production_main(argc,argv);}
