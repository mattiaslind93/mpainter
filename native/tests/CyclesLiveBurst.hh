#pragma once
#include "CyclesLiveUdimArtist.hh"
namespace bts {
using CyclesBurstFrameHook=CyclesFrameHook;
void start_cycles_live_burst(QWidget *,QWidget *,const QString &,const QString &,int,
 std::function<QJsonObject()>,BlenderHost *,CyclesFixtureEdit,CyclesBurstFrameHook);
}
