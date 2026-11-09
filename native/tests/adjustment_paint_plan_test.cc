// SPDX-License-Identifier: GPL-3.0-or-later
#include "AdjustmentPaintPlan.hh"
#include "AdjustmentPaintTransactions.hh"
#include <cassert>
#include <iostream>
using namespace bts;
int main(){
 TextureSet set;TextureLayer layer;layer.kind=LayerKind::Paint;layer.id=QUuid::createUuid();set.channels.fill(true);set.ao_channel=true;set.displacement.channel=true;for(auto &c:set.users)c.present=true;layer.channels.fill(true);layer.ao_enabled=true;layer.displacement.enabled=true;for(auto &c:layer.users)c.enabled=true;
 const auto id=layer.id;const auto opacity=layer.opacity;set_adjustment_layer(layer,true);assert(adjustment_layer(layer));assert(layer.id==id&&layer.opacity==opacity&&!layer.mask);
 for(int c:{0,1,2,4,5,6,7,8,9,10,11,12,13,14,16})assert(adjustment_channel(set,layer,c));
 layer.blending[2]=LayerBlend::Multiply;layer.channels[1]=false;layer.users[3].enabled=false;layer.displacement.blending=LayerBlend::Normal;
 auto token=std::make_shared<AdjustmentPaintSnapshot>();token->id=QUuid::createUuid();token->set=set.id;token->layer=layer.id;token->tiles={1001,1002,1011};
 const auto main=adjustment_main_source(set,layer,token);assert(main.enabled(0)&&!main.enabled(1)&&!main.enabled(2)&&main.enabled(3)&&main.enabled(4));assert(main.channels[3]==6);
 const auto ao=adjustment_scalar_source(set,layer,5,token),user=adjustment_scalar_source(set,layer,7,token),disabled=adjustment_scalar_source(set,layer,10,token),displacement=adjustment_scalar_source(set,layer,16,token);assert(ao.enabled(2)&&ao.channels[2]==5&&user.channels[2]==7&&!disabled&&!displacement);
 assert(main.snapshot->tiles==std::set<int>({1001,1002,1011}));auto frozen=main;token.reset();assert(frozen.snapshot&&frozen.snapshot->layer==id);
 const auto before=layer.channels;set_adjustment_layer(layer,false);assert(!adjustment_layer(layer)&&layer.channels==before);for(int c:{0,1,2,4,5,6,7,8,9,10,11,12,13,14,16})assert(gpu_layer_channel(layer,c).blend==LayerBlend::Normal);
 layer.kind=LayerKind::Fill;bool rejected=false;try{set_adjustment_layer(layer,true);}catch(const std::invalid_argument &){rejected=true;}assert(rejected&&!adjustment_layer(layer));
 layer.kind=LayerKind::Paint;layer.instance=LayerInstance{QUuid::createUuid(),QUuid::createUuid()};rejected=false;try{set_adjustment_layer(layer,true);}catch(const std::invalid_argument &){rejected=true;}assert(rejected&&!adjustment_layer(layer));
 require_adjustment_page_pair(2*adjustment_page_bytes);bool refused=false;try{require_adjustment_page_pair(adjustment_page_bytes);}catch(const std::length_error &error){refused=std::string(error.what()).find("MiB")!=std::string::npos;}assert(refused);
 AdjustmentPaintTransactions transactions;const auto project=QUuid::createUuid(),first=QUuid::createUuid(),second=QUuid::createUuid();int a=1,b=2,foreign=3;
 transactions.record(first,project,set.id,layer.id,10,&a);transactions.record(second,project,set.id,layer.id,11,&b);std::array<std::uintptr_t,2> history{reinterpret_cast<std::uintptr_t>(&a),reinterpret_cast<std::uintptr_t>(&b)};
 assert(transactions.rollback(first,project,11,history)==0);assert(transactions.rollback(second,project,11,history)==1); // release, second gesture, first refusal
 assert(!transactions.rollback(first,project,12,history)); // unrelated edit/Undo revision
 assert(!transactions.rollback(first,QUuid::createUuid(),11,history)); // different project
 history[1]=reinterpret_cast<std::uintptr_t>(&foreign);assert(!transactions.rollback(first,project,11,history)); // changed history tail
 transactions.record(QUuid::createUuid(),project,set.id,QUuid::createUuid(),12,&foreign);assert(!transactions.rollback(first,project,12,history)); // unrelated target gesture
 transactions.clear();assert(!transactions.rollback(first,project,11,history));
 std::cout<<"BM adjustment metadata per-channel routing and immutable token assertions PASS\n";
}
