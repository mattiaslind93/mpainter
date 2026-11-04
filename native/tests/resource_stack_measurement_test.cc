#include "ResourceStack.hh"
#include <array>
#include <cassert>
#include <future>
#include <string>
#include <cstdio>
using namespace bts;
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static std::uintptr_t nested(int depth,std::uintptr_t parent){
 const auto range=resource_stack_range(),saved=range;const auto actual=resource_stack_pointer();assert(actual>=saved.low&&actual<=saved.high);assert(!parent||actual<parent);require_resource_stack(4096,"Nested resource regression");
 if(depth){const auto inner=nested(depth-1,actual);assert(inner<actual);return inner;}
 bool denied=false;try{require_resource_stack(range.high-range.low,"Over-capacity resource regression");}catch(const std::length_error &e){denied=std::string(e.what()).find("available")!=std::string::npos;}assert(denied);return actual;
}
int main(){const auto main=nested(16,0);const auto worker=std::async(std::launch::async,[]{return nested(16,0);}).get();assert(main&&worker&&main!=worker);puts("PASS real frame lies inside native stack, nested frames descend, main/worker over-capacity rejected");}
