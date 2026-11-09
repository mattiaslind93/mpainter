// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlenderHost.hh"
#include "ColorPickerTest.hh"
#include <QApplication>
#include <QJsonDocument>
#include <cstdio>
int main(int argc,char **argv){
 try{QApplication app(argc,argv);bts::BlenderHost host(QCoreApplication::applicationFilePath().toStdString(),"");
  const std::string prior=blender::IMB_colormanagement_working_space_get();const auto result=bts::verify_color_picker_enter();
  if(prior!=blender::IMB_colormanagement_working_space_get())throw std::runtime_error("Color Enter test changed the owner working space");
  std::puts(QJsonDocument(result).toJson(QJsonDocument::Compact).constData());return 0;
 }catch(const std::exception &error){std::fprintf(stderr,"COLOR_PICKER_ENTER_FAILED %s\n",error.what());return 1;}
}
