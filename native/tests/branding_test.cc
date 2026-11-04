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
 assert(bts::product_version()=="0.9.0-alpha");assert(!bts::product_icon().isNull());
 // Bright artwork makes any accidental painting into the footer detectable.
 QImage fixture(1880,1058,QImage::Format_RGB32);fixture.fill(Qt::magenta);
 const auto fixture_path=temp.filePath("portrait.png");assert(fixture.save(fixture_path));
 for(int scale:{1,2}){
  const auto portrait=bts::compose_splash(fixture_path,{.52,.23},scale);
  assert(portrait.pixelColor(230*scale,0)==QColor(Qt::magenta));
  assert(portrait.pixelColor(939*scale,442*scale)==QColor(Qt::magenta));
  for(int y=443*scale;y<portrait.height();++y)for(int x=0;x<portrait.width();++x)
   assert(portrait.pixelColor(x,y)==QColor("#111315"));
 }
 for(int scale:{1,2}){const auto image=bts::compose_splash(temp.filePath("missing.png"),{.5,.5},scale);assert(!image.isNull());assert(image.size()==QSize(940*scale,529*scale));assert(image.pixelColor(10*scale,10*scale)==QColor("#111315"));}
 QMainWindow window;bts::add_product_about(&window);
 assert(window.findChild<QAction *>("downloadFreeMaterialsHelp"));
 auto *about=window.findChild<QAction *>("aboutMPainter");assert(about);bool seen=false;
 QTimer::singleShot(0,&window,[&]{auto *dialog=window.findChild<QMessageBox *>();assert(dialog&&dialog->text().contains("mPainter "+bts::product_version())&&dialog->informativeText().contains("Mattias Lind")&&dialog->informativeText().contains("Not affiliated"));seen=true;dialog->accept();});about->trigger();assert(seen);
}
