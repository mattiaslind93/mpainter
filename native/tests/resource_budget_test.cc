#include "ResourceBudget.hh"
#include "ResourceStack.hh"
#include "StrokeSpacing.hh"
#include <cassert>
#include <iostream>
using namespace bts;
static std::uint64_t memory(){return 32ULL<<30;}
int main(){
 TextureRamQueryScope query(memory);texture_vram_query_valid=true;texture_free_vram=16ULL<<30;
 const ResourceBudget budget;assert(budget.ram()==16ULL<<30);assert(budget.vram()==8ULL<<30);
 require_resource_stack(1024,"Stack fixture");
 bool stack_failed=false;try{require_resource_stack(UINT64_MAX,"Stack fixture");}catch(const std::length_error &){stack_failed=true;}assert(stack_failed);
 // More than the removed 1M triangles, 2M vertices and 256MiB file caps.
 budget.require(6ULL<<30,2ULL<<30,"Five-million-triangle project");
 bool failed=false;try{budget.require(20ULL<<30,0,"fixture");}catch(const std::length_error &e){const std::string text=e.what();failed=text.find("needs RAM")!=text.npos&&text.find("available")!=text.npos&&text.find("MiB")!=text.npos;}assert(failed);
 texture_free_vram=0;failed=false;try{ResourceBudget{}.require(1,1,"VRAM exhausted");}catch(const std::length_error &){failed=true;}assert(failed);
 texture_vram_query_valid=false;ResourceBudget{}.require(1,1,"No driver telemetry");
 failed=false;try{resource_product(UINT64_MAX,2);}catch(const std::overflow_error &){failed=true;}assert(failed);
 failed=false;try{resource_sum(UINT64_MAX,1);}catch(const std::overflow_error &){failed=true;}assert(failed);
 StrokeSpacing spacing;std::size_t dabs=0;spacing.reset({0,0},1);spacing.advance({20000,0},[&](auto){++dabs;return 1.;});assert(dabs==20000);
 spacing.reset({0,0},1e-10);dabs=0;spacing.advance({1e-9,0},[&](auto){++dabs;return 1e-10;});assert(dabs==10);
 std::cout<<"Dynamic RAM/VRAM, exhausted VRAM, unknown driver telemetry and checked arithmetic passed\n";
}
