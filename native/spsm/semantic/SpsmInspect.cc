#include "SpsmSemantic.hh"
#include <iostream>
int main(int argc,char **argv){
 if(argc<2){std::cerr<<"Usage: bts-spsm-inspect FILE.spsm [...]\n";return 2;}
 try{
  std::cout<<"[";for(int i=1;i<argc;++i){if(i>1)std::cout<<',';
   auto inspection=bts::spsm::semantic::inspect_archive(bts::spsm::read_archive(argv[i]));
   bts::spsm::semantic::write_report(std::cout,inspection);
  }std::cout<<"]\n";return std::cout?0:1;
 }catch(const std::exception &e){std::cerr<<"SPSM inspection failed: "<<e.what()<<'\n';return 1;}
}
