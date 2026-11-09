// SPDX-License-Identifier: GPL-3.0-or-later
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "PythonConsole.hh"
#include "ResourceAvailability.hh"
#include "ViewportIdleDemo.hh"
#include "PythonApi.hh"
#include "PythonExtensionApi.hh"
#include "PythonLayerApi.hh"
#include "PythonUi.hh"
#include <QAction>
#include "DesktopOpen.hh"
#include <QDir>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <stdexcept>
#include <optional>
#include <limits>
#include <QTextDocument>
namespace bts {
void start_automation_demo(QMainWindow *, const QString &, std::function<bool()>);
void start_layer_api_demo(QMainWindow *, std::function<bool()>, std::function<QJsonObject()>);
void start_hard_surface_demo(QMainWindow *, std::function<bool()>, std::function<QJsonObject()>);
void start_residency_demo(QMainWindow *,
                          std::function<bool()>,
                          std::function<QJsonObject()>,
                          std::function<QJsonObject()>);
void start_warp_geometry_demo(QMainWindow *, std::function<bool()>, std::function<QJsonObject()>);
void start_fill_symmetry_demo(QMainWindow *, std::function<bool()>, std::function<QJsonObject()>);
void start_channel_configuration_demo(QMainWindow *,
                                      std::function<bool()>,
                                      std::function<QJsonObject()>);
namespace {
struct Gil {
  PyGILState_STATE state = PyGILState_Ensure();
  ~Gil()
  {
    PyGILState_Release(state);
  }
};
struct Bridge {
  QPointer<PythonConsole> owner;
  std::function<QJsonValue(const QString &, const QJsonObject &)> call;
};
Bridge *bridge(PyObject *self)
{
  auto *value = static_cast<Bridge *>(PyCapsule_GetPointer(self, "bts.automation"));
  if (!value)
    return nullptr;
  if (!value->owner) {
    PyErr_SetString(PyExc_RuntimeError, "BTS automation is shut down");
    return nullptr;
  }
  if (QThread::currentThread() != value->owner->thread()) {
    PyErr_SetString(PyExc_RuntimeError, "BTS document API requires the Qt owner thread");
    return nullptr;
  }
  return value;
}
PyObject *native_call(PyObject *self, PyObject *args)
{
  const char *operation, *json;
  Py_ssize_t length;
  if (!PyArg_ParseTuple(args, "ss#", &operation, &json, &length))
    return nullptr;
  auto *context = bridge(self);
  if (!context)
    return nullptr;
  try {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(QByteArray(json, length), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
      throw std::invalid_argument("Automation parameters must be a JSON object");
    const auto result = context->call(QString::fromUtf8(operation), document.object());
    const auto encoded =
        QJsonDocument(QJsonObject{{"result", result}}).toJson(QJsonDocument::Compact);
    return PyUnicode_DecodeUTF8(encoded.data(), encoded.size(), "strict");
  }
  catch (const std::length_error &error) {
    PyErr_SetString(PyExc_MemoryError, error.what());
  }
  catch (const std::invalid_argument &error) {
    PyErr_SetString(PyExc_ValueError, error.what());
  }
  catch (const std::exception &error) {
    PyErr_SetString(PyExc_RuntimeError, error.what());
  }
  return nullptr;
}
PyObject *native_output(PyObject *self, PyObject *args)
{
  const char *text;
  Py_ssize_t length;
  if (!PyArg_ParseTuple(args, "s#", &text, &length))
    return nullptr;
  auto *context = bridge(self);
  if (!context)
    return nullptr;
  try {
    context->owner->output(QString::fromUtf8(text, length));
    Py_RETURN_NONE;
  }
  catch (const std::length_error &error) {
    PyErr_SetString(PyExc_MemoryError, error.what());
  }
  catch (const std::exception &error) {
    PyErr_SetString(PyExc_RuntimeError, error.what());
  }
  return nullptr;
}
PyMethodDef call_def{"call", native_call, METH_VARARGS, "Invoke the native document owner."},
    output_def{"output", native_output, METH_VARARGS, "Write console output."};
void delete_bridge(PyObject *capsule)
{
  delete static_cast<Bridge *>(PyCapsule_GetPointer(capsule, "bts.automation"));
}
}  // namespace
struct PythonConsole::Impl {
  PythonHooks hooks;
  QPlainTextEdit *output = nullptr;
  QLineEdit *command = nullptr;
  QDockWidget *dock = nullptr;
  QTimer ready;
  PyObject *globals = nullptr;
  bool executing = false;
  QString plugins;
  QTimer events;
  QJsonArray queue;
  bool watching = false;
  std::optional<bool> last_busy;
  QString edition_pending;
  std::unique_ptr<PythonUi> ui;
  ~Impl()
  {
    if (globals && Py_IsInitialized()) {
      Gil gil;
      Py_DECREF(globals);
    }
  }
};
PythonConsole::PythonConsole(QMainWindow *window, PythonHooks hooks)
    : QObject(window), impl_(std::make_unique<Impl>())
{
  auto &s = *impl_;
  s.hooks = std::move(hooks);
  if (!Py_IsInitialized())
    throw std::runtime_error("Blender embedded Python is unavailable");
  QSettings settings("mPainter", "Preferences");
  s.plugins = settings
                  .value("Python/pluginFolder",
                         QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                             "/python/plugins")
                  .toString();
  if (!s.hooks.plugin_directory.isEmpty())
    s.plugins = s.hooks.plugin_directory;
  QDir().mkpath(s.plugins);
  s.dock = new QDockWidget("Python Console", window);
  s.dock->setObjectName("pythonConsoleDock");
  auto *body = new QWidget(s.dock);
  auto *layout = new QVBoxLayout(body);
  s.output = new QPlainTextEdit(body);
  s.output->setObjectName("pythonOutput");
  s.output->setReadOnly(true);
  layout->addWidget(s.output, 1);
  auto *row = new QHBoxLayout;
  layout->addLayout(row);
  s.command = new QLineEdit(body);
  s.command->setMaxLength(std::numeric_limits<int>::max());
  s.command->setObjectName("pythonCommand");
  s.command->setPlaceholderText("Execute Python command…");
  row->addWidget(s.command, 1);
  auto *run = new QPushButton("▶", body);
  run->setObjectName("executePythonCommand");
  run->setToolTip("Execute Python command");
  row->addWidget(run);
  s.dock->setWidget(body);
  window->addDockWidget(Qt::BottomDockWidgetArea, s.dock);
  s.dock->hide();
  for (auto *menu : window->menuBar()->findChildren<QMenu *>())
    if (menu->title() == "Views") {
      auto *action = s.dock->toggleViewAction();
      action->setObjectName("showPythonConsole");
      menu->addAction(action);
      break;
    }
  auto *menu = window->menuBar()->addMenu("Python");
  menu->setObjectName("pythonMenu");
  auto *folder = menu->addAction("Plugins Folder");
  folder->setObjectName("pythonPluginsFolder");
  connect(folder, &QAction::triggered, this, [this] {
    open_desktop_url(QUrl::fromLocalFile(plugin_directory()));
  });
  auto *reload = menu->addAction("Reload Plugins Folders");
  reload->setObjectName("reloadPythonPlugins");
  connect(reload, &QAction::triggered, this, [this] {
    const auto json = QJsonDocument(QJsonArray{plugin_directory()}).toJson(QJsonDocument::Compact);
    execute("bts._reload_plugins(" + QString::fromUtf8(json.mid(1, json.size() - 2)) + ")");
  });
  connect(run, &QPushButton::clicked, this, [this] {
    const auto text = impl_->command->text();
    if (text.trimmed().isEmpty())
      return;
    output(">>> " + text + "\n");
    impl_->dock->setProperty("lastExecutionSucceeded", execute(text));
    impl_->command->clear();
  });
  connect(s.command, &QLineEdit::returnPressed, run, &QPushButton::click);
  connect(&s.ready, &QTimer::timeout, this, [this] {
    if (impl_->executing || impl_->hooks.busy())
      return;
    impl_->ready.stop();
    execute("bts._drain_ready()");
  });
  s.ui = std::make_unique<PythonUi>(
      window, [this](const QString &type, const QJsonObject &data) { notify(type, data); });
  connect(&s.events, &QTimer::timeout, this, [this] {
    auto &s = *impl_;
    if (s.executing)
      return;
    if (s.watching) {
      const auto status = s.hooks.call("project.status", {}).toObject();
      const bool busy = status["is_busy"].toBool();
      if (s.last_busy && *s.last_busy != busy)
        notify("BusyStatusChanged", {{"busy", busy}});
      s.last_busy = busy;
      if (!s.edition_pending.isEmpty() && status["is_open"].toBool() && !busy &&
          status["document_id"].toString() == s.edition_pending)
      {
        const auto id = s.edition_pending;
        s.edition_pending.clear();
        notify("ProjectEditionEntered", {{"project_id", id}});
      }
      Gil gil;
      auto *module = PyDict_GetItemString(s.globals, "bts");
      auto *events = module ? PyObject_GetAttrString(module, "event") : nullptr;
      auto *dispatcher = events ? PyObject_GetAttrString(events, "DISPATCHER") : nullptr;
      auto *connections = dispatcher ? PyObject_GetAttrString(dispatcher, "_connections") :
                                       nullptr;
      s.watching = connections && PyMapping_Size(connections) > 0;
      Py_XDECREF(connections);
      Py_XDECREF(dispatcher);
      Py_XDECREF(events);
      if (PyErr_Occurred())
        PyErr_Clear();
    }
    if (!s.queue.isEmpty()) {
      const auto queued = s.queue;
      s.queue = {};
      const auto encoded = QJsonDocument(queued).toJson(QJsonDocument::Compact);
      const auto quoted =
          QJsonDocument(QJsonArray{QString::fromUtf8(encoded)}).toJson(QJsonDocument::Compact);
      execute("bts._deliver_events(__import__('json').loads(" +
              QString::fromUtf8(quoted.mid(1, quoted.size() - 2)) + "))");
    }
    if (!s.watching && s.queue.isEmpty())
      s.events.stop();
  });
  Gil gil;
  auto *state = new Bridge{
      this, [this](const QString &operation, const QJsonObject &parameters) -> QJsonValue {
        if (operation == "events.owner")
          return true;
        if (operation == "events.watch") {
          if (!parameters["enabled"].isBool())
            throw std::invalid_argument("enabled must be boolean");
          impl_->watching = parameters["enabled"].toBool();
          if (impl_->watching) {
            impl_->last_busy = impl_->hooks.busy();
            impl_->events.start(16);
          }
          else if (impl_->queue.isEmpty())
            impl_->events.stop();
          return true;
        }
        if (operation.startsWith("ui."))
          return impl_->ui->call(operation, parameters);
        if (operation == "project.defer") {
          impl_->ready.start(16);
          return QJsonValue();
        }
        return impl_->hooks.call(operation, parameters);
      }};
  PyObject *capsule = PyCapsule_New(state, "bts.automation", delete_bridge);
  PyObject *module = PyModule_New("_bts_host");
  PyModule_AddObject(module, "call", PyCFunction_New(&call_def, capsule));
  PyModule_AddObject(module, "output", PyCFunction_New(&output_def, capsule));
  Py_DECREF(capsule);
  PyDict_SetItemString(PyImport_GetModuleDict(), "_bts_host", module);
  Py_DECREF(module);
  s.globals = PyDict_New();
  PyDict_SetItemString(s.globals, "__builtins__", PyEval_GetBuiltins());
  PyObject *result = PyRun_String(python_api_source, Py_file_input, s.globals, s.globals);
  if (!result) {
    PyErr_Print();
    throw std::runtime_error("Cannot initialize BTS Python API");
  }
  Py_DECREF(result);
  result = PyRun_String(python_extension_api_source, Py_file_input, s.globals, s.globals);
  if (!result) {
    PyErr_Print();
    throw std::runtime_error("Cannot initialize BTS extension API");
  }
  Py_DECREF(result);
  result = PyRun_String(python_layer_api_source, Py_file_input, s.globals, s.globals);
  if (!result) {
    PyErr_Print();
    throw std::runtime_error("Cannot initialize BTS layer API");
  }
  Py_DECREF(result);
  QTimer::singleShot(0, this, [this] { notify("GraphicalUserInterfaceStarted"); });
  if (qEnvironmentVariableIsSet("BTS_AUTOMATION_DEMO"))
    start_automation_demo(window, s.plugins, s.hooks.busy);
  if (qEnvironmentVariableIsSet("BTS_BATCH_CHANNEL_DEMO"))
    start_channel_configuration_demo(window, s.hooks.busy, s.hooks.snapshot);
  if (qEnvironmentVariableIsSet("BTS_FILL_SYMMETRY_DEMO"))
    start_fill_symmetry_demo(window, s.hooks.busy, s.hooks.snapshot);
  if (qEnvironmentVariableIsSet("BTS_IDLE_DEMO_DIR"))
    start_viewport_idle_demo(window, s.hooks.busy);
  if (qEnvironmentVariableIsSet("BTS_RESIDENCY_DEMO"))
    start_residency_demo(window, s.hooks.busy, s.hooks.snapshot, [this] {
      return impl_->hooks.call("display.residency", {}).toObject();
    });
  if (qEnvironmentVariableIsSet("BTS_WARP_GEOMETRY_DEMO"))
    start_warp_geometry_demo(window, s.hooks.busy, s.hooks.snapshot);
  if (qEnvironmentVariableIsSet("BTS_HARD_UNWRAP_DEMO"))
    start_hard_surface_demo(window, s.hooks.busy, s.hooks.snapshot);
  if (qEnvironmentVariableIsSet("BTS_LAYER_API_DEMO"))
    start_layer_api_demo(window, s.hooks.busy, s.hooks.snapshot);
}
PythonConsole::~PythonConsole()
{
  impl_->ready.stop();
  impl_->events.stop();
  if (Py_IsInitialized() && impl_->globals)
    execute("bts._shutdown()");
}
bool PythonConsole::execute(const QString &source)
{
  auto &s = *impl_;
  if (QThread::currentThread() != thread())
    throw std::runtime_error("Python Console requires the owner thread");
  if (s.executing) {
    output("Python Console is already running\n");
    return false;
  }
  s.executing = true;
  struct Reset {
    bool &flag;
    ~Reset()
    {
      flag = false;
    }
  } reset{s.executing};
  Gil gil;
  auto *function = PyDict_GetItemString(s.globals, "_execute");
  const auto bytes = source.toUtf8();
  PyObject *argument = PyUnicode_DecodeUTF8(bytes.data(), bytes.size(), "strict");
  PyObject *result = argument ? PyObject_CallOneArg(function, argument) : nullptr;
  Py_XDECREF(argument);
  if (!result) {
    PyErr_Print();
    return false;
  }
  const bool success = PyObject_IsTrue(result) == 1;
  Py_DECREF(result);
  return success;
}
void PythonConsole::notify(const QString &type, const QJsonObject &data)
{
  auto &s = *impl_;
  if (QThread::currentThread() != thread())
    throw std::logic_error("Events require the owner thread");
  if (type == "ProjectCreated" || type == "ProjectOpened")
    s.edition_pending = data["project_id"].toString();
  if (type == "ProjectEditionLeft" && data["project_id"].toString() == s.edition_pending)
    s.edition_pending.clear();
  if (type.endsWith("AboutToStart") && s.watching && (!s.last_busy || !*s.last_busy)) {
    s.queue.append(
        QJsonObject{{"type", "BusyStatusChanged"}, {"data", QJsonObject{{"busy", true}}}});
    s.last_busy = true;
  }
  if (type == "BakingProcessProgress")
    for (qsizetype n = s.queue.size(); n-- > 0;) {
      const auto previous = s.queue[n].toObject();
      if (previous["type"] == type && previous["data"].toObject()["id"] == data["id"]) {
        s.queue[n] = QJsonObject{{"type", type}, {"data", data}};
        return;
      }
    }
  s.queue.append(QJsonObject{{"type", type}, {"data", data}});
  if (!s.events.isActive())
    s.events.start(16);
}
void PythonConsole::output(const QString &text)
{
  auto *view = impl_->output;
  if (resource_sum(view->document()->characterCount(), text.size()) >
      std::numeric_limits<int>::max())
    throw std::length_error("Console output exceeds Qt document character indexing capacity");
  auto cursor = view->textCursor();
  cursor.movePosition(QTextCursor::End);
  view->setTextCursor(cursor);
  view->insertPlainText(text);
  view->ensureCursorVisible();
}
QString PythonConsole::plugin_directory() const
{
  return impl_->plugins;
}
}  // namespace bts
