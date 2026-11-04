#include "ManyUdimProjectDemo.hh"
#include "NumericLocale.hh"
#include <QApplication>
#include <QSurfaceFormat>
#include <clocale>
#include <cstring>
#include <cstdio>
int main(int argc,char **argv){
 QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);format.setDepthBufferSize(24);QSurfaceFormat::setDefaultFormat(format);bts::initialize_numeric_locale();QApplication app(argc,argv);
 std::setlocale(LC_NUMERIC,"C");if(std::strcmp(std::localeconv()->decimal_point,".")!=0){std::fprintf(stderr,"Native shader numeric locale must use a decimal point\n");return 1;}
 if(argc!=3||QString::fromLocal8Bit(argv[1])!="--headless-many-udim"){std::fprintf(stderr,"usage: many-udim-project-native-test --headless-many-udim OUTPUT_DIRECTORY\n");return 2;}
 try{
  bts::BlenderHost host(QCoreApplication::applicationFilePath().toStdString(),"");QOpenGLContext context;context.setFormat(format);if(!context.create())throw std::runtime_error("Cannot create offscreen GL context");QOffscreenSurface surface;surface.setFormat(context.format());surface.create();if(!surface.isValid()||!context.makeCurrent(&surface))throw std::runtime_error("Cannot activate offscreen surface");QOpenGLFramebufferObject target(384,384,QOpenGLFramebufferObject::CombinedDepthStencil);if(!target.isValid())throw std::runtime_error("Cannot allocate offscreen framebuffer");
  struct Close{bts::BlenderHost &host;~Close(){host.shutdown();}}close{host};host.initialize_gpu(context,surface,target.handle());if(!QString::fromStdString(host.renderer()).contains("NVIDIA",Qt::CaseInsensitive))throw std::runtime_error("NVIDIA hardware renderer required");return bts::verify_many_udim_project(host,context,surface,target,QString::fromLocal8Bit(argv[2]));
 }catch(const std::exception &error){std::fprintf(stderr,"BTS_MANY_UDIM_FAIL %s\n",error.what());return 1;}
}
