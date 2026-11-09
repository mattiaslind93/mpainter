#include "PythonConsole.hh"
#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMainWindow>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QTimer>
namespace bts {
QJsonObject verify_python_extensions(const QString &directory)
{
  QDir().mkpath(directory);
  QMainWindow window;
  auto *menu = window.menuBar()->addMenu("Window");
  menu->addMenu("Views");
  PythonConsole console(
      &window,
      PythonHooks{[](const QString &operation, const QJsonObject &) -> QJsonValue {
                    if (operation == "project.status")
                      return QJsonObject{{"is_open", false}, {"is_busy", false}};
                    throw std::invalid_argument("This CPU fixture only supplies readiness");
                  },
                  [] { return false; },
                  directory + "/plugins"});
  const auto run = [&](const QString &source) {
    if (!console.execute(source)) {
      auto *output = window.findChild<QPlainTextEdit *>("pythonOutput");
      throw std::runtime_error("Python extension CPU assertion: " +
                               output->toPlainText().toStdString());
    }
  };
  const auto spin = [] {
    QEventLoop loop;
    QTimer::singleShot(45, &loop, &QEventLoop::quit);
    loop.exec();
  };
  const auto quoted = QJsonDocument(QJsonArray{directory}).toJson(QJsonDocument::Compact);
  run("root = __import__('pathlib').Path(" + QString::fromUtf8(quoted.mid(1, quoted.size() - 2)) +
      ")");
  run(R"PY(import bts, weakref, gc
received = []
def started(event): received.append(type(event).__name__)
bts.event.DISPATCHER.connect(bts.event.GraphicalUserInterfaceStarted, started)
class Receiver:
    def notify(self, event): received.append(('receiver', event.busy))
weak = Receiver(); weak_reference = weakref.ref(weak)
bts.event.DISPATCHER.connect(bts.event.BusyStatusChanged, weak.notify)
del weak; gc.collect(); assert weak_reference() is None
strong = Receiver(); strong_reference = weakref.ref(strong)
bts.event.DISPATCHER.connect_strong(bts.event.BusyStatusChanged, strong.notify)
del strong; gc.collect(); assert strong_reference() is not None
)PY");
  spin();
  console.notify("BusyStatusChanged", {{"busy", true}});
  spin();
  run(R"PY(assert received.count('GraphicalUserInterfaceStarted') == 1
assert received.count(('receiver', True)) == 1
bts.event.DISPATCHER.disconnect(bts.event.BusyStatusChanged, strong_reference().notify)
gc.collect(); assert strong_reference() is None
clicks = []
dock = bts.ui.add_dock_widget(bts.ui.Panel(bts.ui.Label('initial', object_name='cpuProbeLabel'), bts.ui.Button('Go', lambda: clicks.append(1), object_name='cpuProbeButton')), title='CPU extension', object_name='cpuExtensionDock')
label = dock.find_child('cpuProbeLabel'); assert label.text() == 'initial'
label.set_text('updated'); assert label.text() == 'updated'
button = dock.find_child('cpuProbeButton'); button.trigger()
)PY");
  spin();
  run(R"PY(assert clicks == [1]
layout = bts.ui.get_layout(); bts.ui.set_layout(layout)
bts.ui.delete_ui_element(dock)
assert not dock.is_valid() and not label.is_valid() and not button.is_valid()
rejected = False
try: label.text()
except ValueError: rejected = True
assert rejected
builtin = bts.ui.get_main_window().find_child('pythonOutput')
rejected = False
try: bts.ui.delete_ui_element(builtin)
except ValueError: rejected = True
assert rejected and builtin.is_valid()
plugin = root / 'plugins' / 'cpu_plugin.py'
plugin.write_text("import bts\n" +
 "def changed(event): pass\n" +
 "def start_plugin():\n    global dock\n    bts.event.DISPATCHER.connect_strong(bts.event.BusyStatusChanged, changed)\n    dock = bts.ui.add_dock_widget(bts.ui.Label('Plugin'), object_name='cpuPluginDock')\n" +
 "def close_plugin():\n    raise RuntimeError('intentional close failure')\n")
bts._reload_plugins(str(root / 'plugins'))
)PY");
  if (window.findChildren<QDockWidget *>("cpuPluginDock").size() != 1)
    throw std::runtime_error("Plugin dock not mounted");
  run("bts._reload_plugins(str(root / 'plugins'))");
  if (window.findChildren<QDockWidget *>("cpuPluginDock").size() != 1)
    throw std::runtime_error("Reload duplicated plugin UI");
  run("bts._shutdown(); gc.collect(); assert all(owner != '_bts_plugin_cpu_plugin' for _, _, owner in bts.event.DISPATCHER._connections.values())");
  if (!window.findChildren<QDockWidget *>("cpuPluginDock").isEmpty())
    throw std::runtime_error("Failed-close plugin UI was not cleaned");
  return {{"weak_collected", true},
          {"strong_retained_until_disconnect", true},
          {"gui_started_once", true},
          {"native_qt_widgets", true},
          {"deleted_handles_rejected", true},
          {"builtin_deletion_rejected", true},
          {"reload_unique", true},
          {"failed_close_cleanup", true},
          {"gpu_required", false}};
}
}  // namespace bts
