#include "BakeSamplesWidgetTest.hh"
#include <QFile>
#include <QJsonDocument>
#include <cstdio>
int main(int argc,char **argv){QApplication app(argc,argv);app.setStyle("Fusion");try{bts::bl_widget_test::require(argc==2,"Expected private output directory");const auto result=bts::bl_widget_test::skew(QString::fromLocal8Bit(argv[1]));QFile file(QString::fromLocal8Bit(argv[1])+"/bl-skew-controls.json");bts::bl_widget_test::require(file.open(QIODevice::WriteOnly),"Cannot write widget proof");file.write(QJsonDocument(result).toJson());std::puts("BL actual SkewPanel keyboard/mouse controls:16/128/512+17/511, AA1/2/4/8, seven editable map roles, two-set synchronization PASS");return 0;}catch(const std::exception &e){std::fprintf(stderr,"BL_WIDGET_FAILED %s\n",e.what());return 1;}}
