#include "Theme.hh"
#include "RenderController.hh"
#include "WorkspaceShell.hh"
#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSettings>
#include <QStatusBar>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <vector>
namespace bts {
struct RenderController::Impl {
  WorkspaceShell *shell;
  RenderHooks hooks;
  RenderController *owner;
  bool active = false, syncing = false;
  QString operation_error, last_notice;
  QAction *paint, *render;
  QWidget *panel;
  QTabWidget *tabs;
  QLabel *status, *device_warning;
  QComboBox *device;
  QSpinBox *samples;
  QCheckBox *denoise;
  QTimer timer;
  ViewLayoutState previous_layout;
  QByteArray previous_workspace, previous_center;
  struct Hidden {
    QWidget *widget;
    bool visible;
  };
  std::vector<Hidden> hidden;
  struct Page {
    QDialog *dialog;
    QScrollArea *scroll;
    bool visible;
    QWidget *header;
  };
  std::vector<Page> pages;
  struct Control {
    QWidget *widget;
    bool enabled;
    QString tooltip;
  };
  std::vector<Control> controls;
  std::vector<std::pair<QPushButton *, bool>> buttons;
  std::vector<std::pair<QAction *, bool>> actions;
  Impl(WorkspaceShell *s, RenderHooks h, RenderController *o)
      : shell(s), hooks(std::move(h)), owner(o)
  {
    auto *menu = s->menuBar()->addMenu("&Mode");
    menu->setObjectName("renderModeMenu");
    paint = menu->addAction("Painting — EEVEE");
    paint->setObjectName("paintingMode");
    paint->setShortcut(QKeySequence(Qt::Key_F9));
    render = menu->addAction("Rendering — Cycles");
    render->setObjectName("renderingMode");
    render->setShortcut(QKeySequence(Qt::Key_F10));
    auto *group = new QActionGroup(o);
    for (auto *a : {paint, render}) {
      a->setCheckable(true);
      group->addAction(a);
      s->addAction(a);
    }
    paint->setChecked(true);
    connect(paint, &QAction::triggered, o, [o] { o->set_active(false); });
    connect(render, &QAction::triggered, o, [o] { o->set_active(true); });
    panel = new QWidget(s->workspace_splitter());
    panel->setObjectName("renderSettings");
    panel->setMinimumWidth(320);
    panel->setMaximumWidth(420);
    auto *layout = new QVBoxLayout(panel);
    auto *title = new QLabel("Cycles path tracing", panel);
    layout->addWidget(title);
    status = new QLabel("Initializing Cycles…", panel);
    status->setObjectName("renderStatus");
    status->setWordWrap(true);
    layout->addWidget(status);
    device_warning = new QLabel("GPU unavailable; using CPU.", panel);
    device_warning->setObjectName("cyclesCpuFallbackWarning");
    device_warning->setWordWrap(true);
    device_warning->setStyleSheet(
        bts::theme::qss("background:@Selection@;color:@Warning@;padding:10px;"));
    layout->addWidget(device_warning);
    device_warning->hide();
    device = new QComboBox(panel);
    device->setObjectName("renderDevice");
    device->addItems({"Auto (OptiX / CUDA / CPU)", "CPU"});
    device->setCurrentIndex(QSettings().value("render/force_cpu", false).toBool() ? 1 : 0);
    layout->addWidget(device);
    auto *quality = new QHBoxLayout;
    quality->addWidget(new QLabel("Samples", panel));
    samples = new QSpinBox(panel);
    samples->setObjectName("renderSamples");
    samples->setRange(1, 65536);
    samples->setKeyboardTracking(false);
    quality->addWidget(samples);
    layout->addLayout(quality);
    denoise = new QCheckBox("Denoise", panel);
    denoise->setObjectName("renderDenoise");
    layout->addWidget(denoise);
    tabs = new QTabWidget(panel);
    tabs->setObjectName("renderSettingsTabs");
    layout->addWidget(tabs, 1);
    auto *back = new QPushButton("Return to painting (F9)", panel);
    back->setObjectName("returnToPainting");
    layout->addWidget(back);
    connect(back, &QPushButton::clicked, o, [o] { o->set_active(false); });
    s->workspace_splitter()->addWidget(panel);
    panel->hide();
    connect(device, &QComboBox::currentIndexChanged, o, [this] {
      if (syncing || !active)
        return;
      QSettings().setValue("render/force_cpu", device->currentIndex() == 1);
      try {
        operation_error.clear();
        hooks.end_input();
        hooks.backend(true, device->currentIndex() == 1);
        hooks.repaint();
      }
      catch (const std::exception &e) {
        show_error(QString::fromUtf8(e.what()));
      }
      sync();
    });
    const auto edit = [this] {
      if (syncing || !active)
        return;
      try {
        operation_error.clear();
        hooks.quality(samples->value(), denoise->isChecked());
        hooks.dirty();
        hooks.repaint();
      }
      catch (const std::exception &e) {
        show_error(QString::fromUtf8(e.what()));
      }
    };
    connect(samples, &QSpinBox::valueChanged, o, edit);
    connect(denoise, &QCheckBox::toggled, o, edit);
    timer.setInterval(150);
    connect(&timer, &QTimer::timeout, o, [this] { sync(); });
    timer.start();
    sync();
  }
  void show_error(const QString &message)
  {
    operation_error = message;
    hooks.log("Cycles error: " + message);
    shell->statusBar()->showMessage("Cycles error: " + message);
  }
  void sync()
  {
    syncing = true;
    const auto state = hooks.state();
    const auto error = operation_error.isEmpty() ? state["error"].toString() : operation_error;
    const bool fallback = active && state["cpu_fallback"].toBool();
    device_warning->setText(error.isEmpty() ?
                                state["device_message"].toString() :
                                "Cycles error: " + error +
                                    ". Select CPU to retry, or F9 to return to painting.");
    device_warning->setVisible(active && (fallback || !error.isEmpty()));
    const QString notice = !error.isEmpty() ? device_warning->text() :
                           fallback         ? state["device_message"].toString() :
                                              QString{};
    if (notice != last_notice) {
      if (!notice.isEmpty())
        shell->statusBar()->showMessage(notice);
      else if (shell->statusBar()->currentMessage() == last_notice)
        shell->statusBar()->clearMessage();
      last_notice = notice;
    }
    render->setEnabled(hooks.document() && state["available"].toBool());
    render->setToolTip(hooks.document() ?
                           "Cycles path-traced viewport (F10)" :
                           "Open a project and finish baking to enter rendering mode");
    paint->setChecked(!active);
    render->setChecked(active);
    if (active) {
      const auto message = operation_error.isEmpty() ? state["status"].toString() :
                                                       "Cycles error: " + operation_error;
      status->setText("Cycles / " + state["device"].toString() + "\n" +
                      (message.isEmpty() ? "Initializing Cycles…" : message));
      samples->setValue(state["samples"].toInt(128));
      denoise->setChecked(state["denoise"].toBool());
    }
    syncing = false;
  }
  void enter()
  {
    previous_layout = hooks.layout();
    previous_workspace = shell->workspace_splitter()->saveState();
    previous_center = shell->viewport_shelf_splitter()->saveState();
    for (auto *widget : {shell->findChild<QWidget *>("toolRail"),
                         shell->findChild<QWidget *>("contextToolbar"),
                         shell->findChild<QWidget *>("assetShelf"),
                         static_cast<QWidget *>(shell->inspector_splitter()),
                         shell->findChild<QToolButton *>("displaySettingsButton")->parentWidget()})
      if (widget) {
        hidden.push_back({widget, !widget->isHidden()});
        widget->hide();
      }
    for (auto *name : {"toggleShelf", "toggleConsole", "resetReferenceLayout"})
      if (auto *a = shell->findChild<QAction *>(name)) {
        actions.push_back({a, a->isEnabled()});
        a->setEnabled(false);
      }
    for (auto *name : {"displaySettings", "shaderSettings"}) {
      auto *dialog = shell->findChild<QDialog *>(name);
      if (!dialog)
        continue;
      auto *scroll = dialog->findChild<QScrollArea *>();
      if (!scroll)
        continue;
      QWidget *header = nullptr;
      if (QString(name) == "shaderSettings")
        if (auto *instance = dialog->findChild<QWidget *>("shaderInstanceName"))
          header = instance->parentWidget();
      pages.push_back({dialog, scroll, dialog->isVisible(), header});
      if (QString(name) == "shaderSettings") {
        shell->findChild<QToolButton *>("shaderSettingsButton")->click();
        if (header) {
          dialog->layout()->removeWidget(header);
          qobject_cast<QVBoxLayout *>(scroll->widget()->layout())->insertWidget(0, header);
          header->show();
        }
      }
      dialog->hide();
      dialog->layout()->removeWidget(scroll);
      tabs->addTab(scroll,
                   QString(name) == "displaySettings" ? "Display Settings" : "Shader Settings");
      for (auto *b : scroll->findChildren<QPushButton *>())
        if (b->text() == "Dock" || b->text() == "Undock" || b->text() == "Close") {
          buttons.push_back({b, b->isEnabled()});
          b->setEnabled(false);
        }
    }
    for (auto *name : {"environmentBlur", "environmentBlurSlider", "subsurfacePreviewSamples"})
      if (auto *w = shell->findChild<QWidget *>(name)) {
        controls.push_back({w, w->isEnabled(), w->toolTip()});
        w->setEnabled(false);
        w->setProperty("renderUnavailable", true);
        w->setToolTip(
            QString(name) == "subsurfacePreviewSamples" ?
                "EEVEE diffusion samples; Cycles uses the rendering sample limit above." :
                "Environment blur is available in EEVEE preview; Cycles uses the original HDRI.");
      }
    hooks.rendering(true);
    hooks.set_layout({ViewLayout::SceneOnly, false});
    panel->show();
    shell->workspace_splitter()->setSizes({1000, 0, 360});
  }
  void leave()
  {
    panel->hide();
    for (const auto &control : controls) {
      control.widget->setProperty("renderUnavailable", false);
      control.widget->setEnabled(
          control.widget->objectName() == "subsurfacePreviewSamples" ?
              shell->findChild<QCheckBox *>("subsurfacePreviewEnabled")->isChecked() :
              control.enabled);
      control.widget->setToolTip(control.tooltip);
    }
    controls.clear();
    for (auto &page : pages) {
      if (page.header) {
        page.scroll->widget()->layout()->removeWidget(page.header);
        qobject_cast<QVBoxLayout *>(page.dialog->layout())->insertWidget(0, page.header);
        page.header->show();
      }
      tabs->removeTab(tabs->indexOf(page.scroll));
      page.dialog->layout()->addWidget(page.scroll);
      page.scroll->show();
      if (page.dialog->objectName() == "shaderSettings" && !page.visible)
        page.dialog->done(0);
      else
        page.dialog->setVisible(page.visible);
    }
    pages.clear();
    for (auto &[button, enabled] : buttons)
      button->setEnabled(enabled);
    buttons.clear();
    for (auto &[action, enabled] : actions)
      action->setEnabled(enabled);
    actions.clear();
    for (const auto &item : hidden)
      item.widget->setVisible(item.visible);
    hidden.clear();
    shell->workspace_splitter()->restoreState(previous_workspace);
    shell->viewport_shelf_splitter()->restoreState(previous_center);
    hooks.rendering(false);
    hooks.set_layout(previous_layout);
  }
};
RenderController::RenderController(WorkspaceShell *shell, RenderHooks hooks)
    : QObject(shell), impl_(std::make_unique<Impl>(shell, std::move(hooks), this))
{
  shell->installEventFilter(this);
}
RenderController::~RenderController() = default;
bool RenderController::active() const
{
  return impl_->active;
}
void RenderController::set_active(bool value)
{
  auto &s = *impl_;
  if (s.active == value) {
    s.sync();
    return;
  }
  if (value && !s.hooks.document()) {
    s.sync();
    return;
  }
  try {
    s.operation_error.clear();
    s.hooks.end_input();
    s.hooks.backend(value, s.device->currentIndex() == 1);
    if (value)
      s.enter();
    else
      s.leave();
    s.active = value;
    s.hooks.repaint();
  }
  catch (const std::exception &e) {
    s.show_error(QString::fromUtf8(e.what()));
  }
  s.sync();
}
void RenderController::reset_document()
{
  if (active())
    set_active(false);
}
bool RenderController::eventFilter(QObject *watched, QEvent *event)
{
  if (watched == impl_->shell && event->type() == QEvent::Close)
    reset_document();
  return QObject::eventFilter(watched, event);
}
}  // namespace bts
