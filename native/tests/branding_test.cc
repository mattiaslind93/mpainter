#include "Branding.hh"
#include <QApplication>
#include <QTemporaryDir>
#include <QMainWindow>
#include <QMessageBox>
#include <QAction>
#include <QTimer>
#include <cassert>
int main(int argc,char **argv){
 QApplication app(argc,argv);QTemporaryDir temp;assert(temp.isValid());
 assert(bts::product_version()=="1.0.0");assert(!bts::product_icon().isNull());
 for(int scale:{1,2}){const auto image=bts::compose_splash(temp.filePath("missing.png"),{.5,.5},scale);assert(!image.isNull());assert(image.size()==QSize(940*scale,529*scale));assert(image.pixelColor(10*scale,10*scale)==QColor("#111315"));}
 QMainWindow window;bts::add_product_about(&window);
 assert(window.findChild<QAction *>("downloadFreeMaterialsHelp"));
 auto *about=window.findChild<QAction *>("aboutMPainter");assert(about);bool seen=false;
 QTimer::singleShot(0,&window,[&]{auto *dialog=window.findChild<QMessageBox *>();assert(dialog&&dialog->text().contains("mPainter")&&dialog->informativeText().contains("Mattias Lind")&&dialog->informativeText().contains("Not affiliated"));seen=true;dialog->accept();});about->trigger();assert(seen);
}
