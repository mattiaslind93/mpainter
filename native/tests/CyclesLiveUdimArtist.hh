#pragma once
#include "../host/blender/BlenderHost.hh"
#include "../host/document/TextureDocument.hh"
#include <QJsonObject>
#include <QString>
#include <functional>
class QWidget;
namespace bts {
using CyclesFrameHook=std::function<void(const std::function<void()> &)>;
using CyclesFixtureEdit=std::function<void(const std::function<void(TextureSet &)> &)>;
void start_cycles_udim_artist_demo(QWidget *,QWidget *,const QString &,const QString &,int,
 std::function<QJsonObject()>,BlenderHost *,CyclesFixtureEdit,CyclesFrameHook={});
}
