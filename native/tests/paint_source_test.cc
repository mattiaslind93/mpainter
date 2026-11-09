// SPDX-License-Identifier: GPL-3.0-or-later
#include "PaintRasterSource.hh"
#include "PaintRecipe.hh"
#include <cassert>
#include <iostream>
using namespace bts;
struct Source final:PaintRasterSource {
  PaintPixels input;int *evaluations;
  Source(PaintPixels value,int &count):input(std::move(value)),evaluations(&count){}
  PaintPixels evaluate_cpu()const override {++*evaluations;return input.materialized();}
  std::vector<const PaintPixels *> cpu_inputs()const override {return {&input};}
};
int main()
{
  PaintPixels reference;
  PaintPixels::Stamp stamp{{.3,.3},{.15,0},{0,.15}};
  stamp.channels={true,false,false,false,true};
  stamp.values.base_color={1.4,.2,.7,1};stamp.values.bump=-.3;
  reference.stamp(128,stamp);
  int evaluations=0;
  const auto before=PaintPixels::recipe_materializations();
  auto source=std::make_shared<Source>(reference,evaluations);
  auto authored=PaintPixels::from_source(128,source,reference.page_versions());
  {
    PaintMetadataScope metadata;
    assert(!authored.empty()&&authored.page_count(0)==reference.page_count(0));
    assert(authored.page_versions()!=reference.page_versions());
  }
  auto history=authored;
  {
    PaintRecordingScope record;
    stamp.center={.7,.7};authored.stamp(128,stamp);
  }
  assert(!evaluations&&PaintPixels::recipe_materializations()==before);
  {
    GpuPersistenceScope gpu_only;bool denied=false;
    try{history.hash(0);}catch(const std::logic_error &){denied=true;}
    assert(denied&&!evaluations&&PaintPixels::recipe_materializations()==before);
    PaintMetadataScope metadata;assert(history.page_count(0)==reference.page_count(0));
    assert(reference.hash(0)); // Canonical bytes need no CPU raster evaluation.
  }

  assert(history.hash(0)==reference.hash(0)&&history.hash(4)==reference.hash(4));
  assert(evaluations==1);
  reference.stamp(128,stamp);
  assert(authored.hash(0)==reference.hash(0)&&authored.hash(4)==reference.hash(4));
  assert(evaluations==1); // Cached source, independent later stroke.
  bool rejected=false;try{PaintPixels::PageVersions outside;outside[0][2]=1;PaintPixels::from_source(128,source,outside);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
  PaintPixels::PageVersions incomplete;incomplete[0][0]=1;
  auto bad=PaintPixels::from_source(128,source,incomplete);
  rejected=false;try{bad.materialized();}catch(const std::logic_error &){rejected=true;}assert(rejected);
  const auto pages=reference.page_versions();
  // Source-owned dependencies are retired iteratively too, without recursive
  // destruction of a long immutable history chain.
  {
    auto chain=reference;
    for(int i=0;i<20000;++i)chain=PaintPixels::from_source(128,std::make_shared<Source>(chain,evaluations),pages);
  }
  assert(evaluations==2); // Only the deliberately invalid CPU reference above.
  std::cout<<"Lazy procedural paint sources: sparse metadata, shared snapshots, CPU oracle, bounds and iterative retirement passed\n";
}
