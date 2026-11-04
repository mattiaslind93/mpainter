#pragma once
#include <QString>
#include <exception>
namespace bts {
// Shared by paintGL and the CPU fault-injection test. Once failed, incidental
// Qt paint events cannot re-enter the renderer until a new project is adopted.
class ViewportDrawGuard {
  bool failed_=false;
 public:
  bool failed() const {return failed_;}
  void reset(){failed_=false;}
  template<class Draw,class Failure> void run(Draw &&draw,Failure &&failure) {
    if(failed_)return;
    QString message;
    try{draw();return;}
    catch(const std::exception &error){message=QString::fromUtf8(error.what());}
    catch(...){message="Unknown native draw exception";}
    failed_=true;
    failure("Viewport rendering stopped: "+message);
  }
};
}
