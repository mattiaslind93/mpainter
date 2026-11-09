// SPDX-License-Identifier: GPL-3.0-or-later
#include "PythonUi.hh"
#include "ResourceStack.hh"
#include "WorkspaceShell.hh"
#include <QAction>
#include <QSet>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <QDockWidget>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QToolBar>
#include <QUuid>
#include <QVBoxLayout>
namespace bts {
namespace {
QString text(const QJsonObject &data, const char *key)
{
  if (!data[key].isString() || data[key].toString().contains(QChar(0)))
    throw std::invalid_argument("Invalid UI text");
  const auto value = data[key].toString();
  return value;
}
void require_document_text(const QString &value)
{
  if (value.size() >= std::numeric_limits<int>::max())
    throw std::length_error("Text exceeds Qt document character indexing capacity");
}
}  // namespace
struct PythonUi::Impl {
  struct Item {
    QPointer<QObject> object;
    QString owner, parent;
    QPointer<QObject> attachment;
    bool owned = true;
  };
  QMainWindow *window;
  Notify notify;
  QMap<QString, Item> items;
  QByteArray initial;
  QPointer<QToolBar> plugins;
  explicit Impl(QMainWindow *parent, Notify events)
      : window(parent), notify(std::move(events)), initial(parent->saveState())
  {
  }
  QObject *object(const QString &id)
  {
    if (id == "main")
      return window;
    const auto found = items.find(id);
    if (found == items.end() || !found->object)
      throw std::invalid_argument("UI handle has been deleted or is unknown");
    return found->object;
  }
  QString add(QObject *value,
              const QString &owner,
              const QString &parent,
              QObject *attachment = nullptr,
              bool owned = true)
  {
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    items[id] = {value, owner, parent, attachment, owned};
    return id;
  }
  void name(QObject *value, const QJsonObject &data)
  {
    const auto name = text(data, "object_name");
    static const QRegularExpression valid("^[A-Za-z_][A-Za-z0-9_.:-]*$");
    if (!valid.match(name).hasMatch() ||
        (window->findChild<QObject *>(name) && window->findChild<QObject *>(name) != value))
      throw std::invalid_argument(
          "UI object_name must be unique and contain letters, digits, _, ., : or -");
    value->setObjectName(name);
  }
  QWidget *widget(const QJsonObject &spec, QWidget *parent)
  {
    require_resource_stack(sizeof(QWidget) + sizeof(QVBoxLayout) + sizeof(QJsonObject) +
                               sizeof(QJsonArray) + 4 * sizeof(QString),
                           "Extension UI widget nesting");
    const auto type = text(spec, "type");
    QWidget *result = nullptr;
    if (type == "panel") {
      auto *panel = new QWidget(parent);
      auto *layout = new QVBoxLayout(panel);
      if (!spec["children"].isArray()) {
        delete panel;
        throw std::invalid_argument("Panel children must be an array");
      }
      try {
        for (const auto &entry : spec["children"].toArray()) {
          if (!entry.isObject())
            throw std::invalid_argument("Invalid child widget");
          layout->addWidget(widget(entry.toObject(), panel));
        }
        layout->addStretch();
      }
      catch (...) {
        delete panel;
        throw;
      }
      result = panel;
    }
    else if (type == "label") {
      auto *label = new QLabel(text(spec, "text"), parent);
      label->setWordWrap(true);
      result = label;
    }
    else if (type == "edit") {
      const auto contents = text(spec, "text");
      require_document_text(contents);
      auto *edit = new QLineEdit(parent);
      edit->setMaxLength(std::numeric_limits<int>::max());
      edit->setText(contents);
      result = edit;
    }
    else if (type == "text") {
      const auto contents = text(spec, "text");
      require_document_text(contents);
      auto *view = new QPlainTextEdit(contents, parent);
      view->setReadOnly(true);
      result = view;
    }
    else if (type == "button") {
      auto *button = new QPushButton(text(spec, "text"), parent);
      const auto token = text(spec, "callback");
      QObject::connect(button, &QPushButton::clicked, window, [this, token] {
        notify("UiCallback", {{"token", token}});
      });
      result = button;
    }
    else
      throw std::invalid_argument("Unknown native UI widget type");
    if (spec.contains("object_name")) {
      const auto value = spec["object_name"].toString();
      if (!value.isEmpty())
        result->setObjectName(value);
    }
    return result;
  }
  QJsonArray remove(const QString &id)
  {
    if (id == "main")
      throw std::invalid_argument("Cannot delete the application window");
    (void)object(id);
    if (!items[id].owned)
      throw std::invalid_argument("Built-in UI cannot be deleted");
    QSet<QString> deleted{id};
    bool changed = true;
    while (changed) {
      changed = false;
      for (auto it = items.begin(); it != items.end(); ++it)
        if (deleted.contains(it->parent) && !deleted.contains(it.key())) {
          deleted.insert(it.key());
          changed = true;
        }
    }
    const auto root = items[id];
    if (auto *dock = qobject_cast<QDockWidget *>(root.object.data()))
      window->removeDockWidget(dock);
    if (auto *bar = qobject_cast<QToolBar *>(root.object.data()))
      window->removeToolBar(bar);
    QJsonArray ids;
    for (const auto &key : deleted) {
      ids.append(key);
      items.remove(key);
    }
    if (root.attachment)
      delete root.attachment;
    else if (root.object)
      delete root.object;
    if (plugins && plugins->actions().isEmpty()) {
      window->removeToolBar(plugins);
      delete plugins;
      plugins = nullptr;
    }
    notify("UiDeleted", {{"ids", ids}});
    return ids;
  }
};
PythonUi::PythonUi(QMainWindow *window, Notify notify)
    : impl_(std::make_unique<Impl>(window, std::move(notify)))
{
}
PythonUi::~PythonUi()
{
  auto &s = *impl_;
  while (!s.items.isEmpty()) {
    const auto id = s.items.firstKey();
    if (!s.items[id].object || !s.items[id].owned) {
      s.items.remove(id);
      continue;
    }
    s.remove(id);
  }
}
QJsonValue PythonUi::call(const QString &operation, const QJsonObject &data)
{
  auto &s = *impl_;
  if (operation == "ui.main_window")
    return QJsonObject{{"id", "main"}, {"object_name", s.window->objectName()}};
  if (operation == "ui.remove_owner") {
    const auto owner = text(data, "owner");
    QJsonArray deleted;
    while (true) {
      const auto item = std::find_if(
          s.items.begin(), s.items.end(), [&](const auto &entry) { return entry.owner == owner; });
      if (item == s.items.end())
        break;
      const auto id = item.key();
      if (!item->object) {
        s.items.remove(id);
        deleted.append(id);
      }
      else
        for (const auto &entry : s.remove(id))
          deleted.append(entry);
    }
    return deleted;
  }
  if (operation == "ui.get_layout") {
    const auto *workspace = dynamic_cast<WorkspaceShell *>(s.window);
    return QJsonObject{
        {"qt", QString::fromLatin1(s.window->saveState().toBase64())},
        {"workspace",
         workspace ? QString::fromLatin1(workspace->save_layout().toBase64()) : QString()}};
  }
  if (operation == "ui.set_layout") {
    const auto qt = QByteArray::fromBase64(text(data, "qt").toLatin1()),
               workspace = QByteArray::fromBase64(text(data, "workspace").toLatin1());
    const auto before = s.window->saveState();
    if (!s.window->restoreState(qt))
      throw std::invalid_argument("Invalid Qt layout state");
    if (auto *shell = dynamic_cast<WorkspaceShell *>(s.window);
        shell && !shell->restore_layout(workspace))
    {
      s.window->restoreState(before);
      throw std::invalid_argument("Invalid workspace layout state");
    }
    return true;
  }
  if (operation == "ui.reset_layout") {
    s.window->restoreState(s.initial);
    if (auto *shell = dynamic_cast<WorkspaceShell *>(s.window))
      shell->reset_reference_layout();
    return true;
  }
  if (operation == "ui.create") {

    const auto kind = text(data, "kind"), owner = text(data, "owner"),
               parent = data.value("parent").toString("main");
    auto *container = s.object(parent);
    std::unique_ptr<QObject> pending;
    QObject *attachment = nullptr;
    if (kind == "dock") {
      auto dock = std::make_unique<QDockWidget>(text(data, "title"), s.window);
      s.name(dock.get(), data);
      if (!data["widget"].isObject())
        throw std::invalid_argument("Dock requires a widget description");
      dock->setWidget(s.widget(data["widget"].toObject(), dock.get()));
      s.window->addDockWidget(Qt::RightDockWidgetArea, dock.get());
      for (auto *menu : s.window->menuBar()->findChildren<QMenu *>())
        if (menu->title() == "Views") {
          menu->addAction(dock->toggleViewAction());
          break;
        }
      pending = std::move(dock);
    }
    else if (kind == "menu") {
      auto menu = std::make_unique<QMenu>(text(data, "title"), s.window);
      s.name(menu.get(), data);
      if (auto *outer = qobject_cast<QMenu *>(container))
        outer->addMenu(menu.get());
      else if (container == s.window)
        s.window->menuBar()->addMenu(menu.get());
      else
        throw std::invalid_argument("Menu parent must be main window or menu");
      pending = std::move(menu);
    }
    else if (kind == "toolbar") {
      auto bar = std::make_unique<QToolBar>(text(data, "title"), s.window);
      s.name(bar.get(), data);
      s.window->addToolBar(bar.get());
      pending = std::move(bar);
    }
    else if (kind == "action") {
      auto action = std::make_unique<QAction>(text(data, "title"), container);
      s.name(action.get(), data);
      const auto token = text(data, "callback");
      QObject::connect(action.get(), &QAction::triggered, s.window, [&s, token] {
        s.notify("UiCallback", {{"token", token}});
      });
      if (auto *menu = qobject_cast<QMenu *>(container))
        menu->addAction(action.get());
      else if (auto *bar = qobject_cast<QToolBar *>(container))
        bar->addAction(action.get());
      else if (container == s.window)
        s.window->menuBar()->addAction(action.get());
      else
        throw std::invalid_argument("Action parent must be main window, menu or toolbar");
      pending = std::move(action);
    }
    else if (kind == "toolbar_widget") {
      auto *bar = qobject_cast<QToolBar *>(container);
      if (container == s.window) {
        if (!s.plugins) {
          s.plugins = new QToolBar("Plugins", s.window);
          s.plugins->setObjectName("pythonPluginsToolbar");
          s.window->addToolBar(s.plugins);
        }
        bar = s.plugins;
      }
      if (!bar)
        throw std::invalid_argument("Widget parent must be toolbar");
      if (!data["widget"].isObject())
        throw std::invalid_argument("Toolbar requires a widget description");
      pending.reset(s.widget(data["widget"].toObject(), bar));
      s.name(pending.get(), data);
      attachment = bar->addWidget(static_cast<QWidget *>(pending.get()));
      bar->show();
    }
    else
      throw std::invalid_argument("Unknown UI element kind");
    const auto id = s.add(pending.get(), owner, parent, attachment);
    pending.release();
    return QJsonObject{{"id", id}, {"object_name", s.object(id)->objectName()}};
  }
  const auto id = text(data, "id");
  if (operation == "ui.is_valid")
    return id == "main" || (s.items.contains(id) && s.items[id].object);
  if (operation == "ui.delete")
    return s.remove(id);
  auto *value = s.object(id);
  if (operation == "ui.find_child") {
    auto *child = value->findChild<QObject *>(text(data, "object_name"));
    if (!child)
      throw std::invalid_argument("Named child widget not found");
    for (auto it = s.items.begin(); it != s.items.end(); ++it)
      if (it->object == child)
        return QJsonObject{{"id", it.key()}, {"object_name", child->objectName()}};
    return QJsonObject{{"id",
                        s.add(child,
                              id == "main" ? QString() : s.items[id].owner,
                              id,
                              nullptr,
                              id != "main" && s.items[id].owned)},
                       {"object_name", child->objectName()}};
  }
  if (operation == "ui.set_visible") {
    if (!data["visible"].isBool())
      throw std::invalid_argument("visible must be boolean");
    auto *widget = qobject_cast<QWidget *>(value);
    if (!widget)
      throw std::invalid_argument("UI element is not a widget");
    widget->setVisible(data["visible"].toBool());
    return true;
  }
  if (operation == "ui.trigger") {
    if (auto *action = qobject_cast<QAction *>(value))
      action->trigger();
    else if (auto *button = qobject_cast<QPushButton *>(value))
      button->click();
    else
      throw std::invalid_argument("UI element is not an action or button");
    return true;
  }
  if (operation == "ui.get_text") {
    if (auto *label = qobject_cast<QLabel *>(value))
      return label->text();
    if (auto *edit = qobject_cast<QLineEdit *>(value))
      return edit->text();
    if (auto *view = qobject_cast<QPlainTextEdit *>(value))
      return view->toPlainText();
    if (auto *action = qobject_cast<QAction *>(value))
      return action->text();
    throw std::invalid_argument("UI element has no text field");
  }
  if (operation == "ui.set_text") {
    const auto contents = text(data, "text");
    if (auto *label = qobject_cast<QLabel *>(value))
      label->setText(contents);
    else if (auto *edit = qobject_cast<QLineEdit *>(value)) {
      require_document_text(contents);
      edit->setText(contents);
    }
    else if (auto *view = qobject_cast<QPlainTextEdit *>(value)) {
      require_document_text(contents);
      view->setPlainText(contents);
    }
    else if (auto *action = qobject_cast<QAction *>(value))
      action->setText(contents);
    else
      throw std::invalid_argument("UI element has no text field");
    return true;
  }
  throw std::invalid_argument("Unknown UI operation");
}
}  // namespace bts
