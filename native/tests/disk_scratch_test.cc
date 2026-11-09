// SPDX-License-Identifier: GPL-3.0-or-later
#include "DiskScratch.hh"
#include <QCoreApplication>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <sstream>
struct Point {float x=0,y=0,z=1;bool valid=false;};
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);
 bts::TextureRamQueryScope scarce(+[]()->std::uint64_t{return 1;});
 bts::ScratchVector<Point> points(1024*1024);assert(points[0].z==1&&!points[0].valid);
#if defined(__linux__)
 bool backed=false;std::ifstream maps("/proc/self/maps");std::string line;const auto address=reinterpret_cast<std::uintptr_t>(points.data());
 while(std::getline(maps,line)){std::istringstream row(line);std::uintptr_t low=0,high=0;char dash=0;row>>std::hex>>low>>dash>>high;if(dash=='-'&&address>=low&&address<high)backed=line.find("/scratch-")!=std::string::npos&&line.find("(deleted)")!=std::string::npos;}
 assert(backed);
#endif
 for(std::size_t i=0;i<points.size();++i){points[i].x=float(i);points[i].valid=true;}
 auto copied=points;copied[27].x=-1;assert(points[27].x==27);
 auto *original=points.data();auto moved=std::move(points);assert(moved.data()==original&&moved.size()==1024*1024);
 for(std::size_t i=0;i<moved.size();++i)assert(moved[i].x==float(i)&&moved[i].z==1&&moved[i].valid);
 bts::ScratchVector<Point> from_vector;from_vector=std::vector<Point>(3);assert(from_vector.size()==3&&from_vector[2].z==1);
 moved.resize(2*1024*1024);assert(moved[27].x==27&&moved.back().z==1&&!moved.back().valid);
}
