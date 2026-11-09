// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CyclesLiveUdimArtist.hh"
namespace bts {
void start_cycles_displacement_artist_demo(QWidget *,QWidget *,const QString &,const QString &,int,
 std::function<QJsonObject()>,BlenderHost *,CyclesFixtureEdit,CyclesFrameHook);
}
