#include "ProfileApplication.hh"
#include <QTimer>
#include <thread>
int main(int argc, char **argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("BTS_PERFORMANCE_PROFILE", "1");
  qputenv("BTS_SESSION_PROFILE", "1");
  bts::ProfileApplication app(argc, argv);
  QTimer::singleShot(0, &app, [&] {
    {
      bts::performance::Scope phase("artificial_150ms_block");
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    app.quit();
  });
  return app.exec();
}
