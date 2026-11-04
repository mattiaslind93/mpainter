#include "ViewportDrawGuard.hh"
#include <QApplication>
#include <QLabel>
#include <QTimer>
#include <cassert>
#include <stdexcept>
int main(int argc,char **argv){
  QApplication app(argc,argv);QWidget viewport;QLabel error(&viewport);QTimer frames;
  bts::ViewportDrawGuard guard;int draws=0,checkpoints=0;bool event_loop_alive=false;
  const auto failed=[&](const QString &message){frames.stop();error.setText(message);error.show();QTimer::singleShot(0,[&]{++checkpoints;});};
  frames.start(16);guard.run([&]{++draws;throw std::runtime_error("injected host.draw allocation failure");},failed);
  assert(guard.failed()&&!frames.isActive());assert(error.text().contains("injected host.draw"));assert(!error.isHidden());
  guard.run([&]{++draws;},failed);assert(draws==1);
  QTimer::singleShot(1,[&]{event_loop_alive=true;app.quit();});app.exec();assert(event_loop_alive&&checkpoints==1);
  guard.reset();guard.run([&]{++draws;},failed);assert(draws==2&&!guard.failed());
  guard.run([]{throw 42;},failed);assert(guard.failed()&&error.text().contains("Unknown native"));
}
