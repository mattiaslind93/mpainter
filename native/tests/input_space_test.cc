#include "FillTextureJSON.hh"
#include <cassert>
#include <cstdio>
using namespace bts;
int main(){
 auto automatic=std::make_shared<FillTexture>();automatic->asset="c36ae466-dbad-43fc-b48e-b58e42b06011";automatic->hash=std::string(64,'a');automatic->name="Shared texture";automatic->width=automatic->height=2;automatic->pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(4,FillTexture::Pixel{.21586f,.21586f,.21586f,1});automatic->validate();const auto old=fill_texture_json(automatic);assert(!old.toObject().contains("source_space"));assert(read_fill_texture_json(old)->source_space.empty());
 auto linear=std::make_shared<FillTexture>(*automatic);linear->source_space="ACEScg";linear->pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(4,FillTexture::Pixel{.50196f,.50196f,.50196f,1});linear->validate();assert(read_fill_texture_json(fill_texture_json(linear))->source_space=="ACEScg");assert(automatic->source_space.empty()&&automatic->sample(.5,.5)[0]<.22&&linear->sample(.5,.5)[0]>.5);
 for(int kind=0;kind<2;++kind){auto bad=fill_texture_json(linear).toObject();if(kind==0)bad["raw"]=true;else bad["source_space"]=false;bool failed=false;try{read_fill_texture_json(bad);}catch(...){failed=true;}assert(failed);}
 // This is serialized metadata, not a claim that the OCIO registry contains
 // this long alias. Its native lookup is validated separately during decoding.
 {auto data=fill_texture_json(linear).toObject();const QString label(4096,'x');data["source_space"]=label;assert(QString::fromStdString(read_fill_texture_json(data)->source_space)==label);TextureRamQueryScope low(+[]()->std::uint64_t{return 0;});bool denied=false;try{read_fill_texture_json(data);}catch(const std::length_error &){denied=true;}assert(denied);}
 auto sequence=std::make_shared<FillTexture>(*linear);auto tiles=std::make_shared<std::map<int,std::shared_ptr<const FillTexture>>>();(*tiles)[1001]=linear;(*tiles)[1002]=linear;sequence->tiles=tiles;sequence->validate();assert(read_fill_texture_json(fill_texture_json(sequence))->tiles->at(1002)->source_space=="ACEScg");(*tiles)[1002]=automatic;bool failed=false;try{sequence->validate();}catch(...){failed=true;}assert(failed);
 std::puts("Per-usage input space, immutable shared resource identity, exact descriptors, legacy Auto, raw validation and UDIM consistency PASS");
}
