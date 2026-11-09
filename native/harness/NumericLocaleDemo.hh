#pragma once
#include "NumericLocale.hh"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QAbstractButton>
#include <QKeyEvent>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPushButton>
#include <QToolButton>
namespace bts {
class NumericLocaleDemo final : public QObject {
  QWidget *w;
  QString output, mesh;
  QTimer timer;
  std::function<QJsonObject()> state;
  int stage = 0, ticks = 0;
  bool busy = false;
  template<class T> T *get(const char *name)
  {
    auto *control = w->findChild<T *>(name);
    if (!control)
      qFatal("Numeric locale control missing %s", name);
    return control;
  }
  void advance()
  {
    if (busy)
      return;
    if (++ticks > 1500)
      qFatal("Numeric locale demo timeout %d", stage);
    const auto data = state();
    if (data["composite_pending"].toBool() || data["property_pending"].toBool())
      return;
    switch (stage++) {
      case 0:
        get<QAction>("newProject")->trigger();
        break;
      case 1:
        get<QComboBox>("projectTemplate")->setCurrentIndex(0);
        get<QLineEdit>("meshPath")->setText(mesh);
        get<QComboBox>("documentResolution")
            ->setCurrentIndex(get<QComboBox>("documentResolution")->findData(128));
        busy = true;
        get<QPushButton>("createProject")->click();
        busy = false;
        break;
      case 2:
        get<QToolButton>("addFillLayer")->click();
        break;
      case 3: {
        auto *spin = get<QDoubleSpinBox>("roughnessControl");
        spin->setFocus();
        spin->selectAll();
        for (QChar c : QString("0,25")) {
          QKeyEvent event(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
          QCoreApplication::sendEvent(spin, &event);
        }
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(spin, &enter);
        break;
      }
      case 4: {
        auto *spin = get<QDoubleSpinBox>("roughnessControl");
        if (spin->value() != .25)
          qFatal("Comma input was not interpreted as 0.25");
        QJsonArray numbers, texts;
        for (auto *widget : w->findChildren<QWidget *>()) {
          QString text;
          if (auto *number = qobject_cast<QDoubleSpinBox *>(widget)) {
            text = number->text();
            if (text.contains(',') || number->locale().decimalPoint() != ".")
              qFatal("Comma decimal remains in %s", qPrintable(number->objectName()));
            numbers.append(QJsonObject{{"name", number->objectName()}, {"text", text}});
          }
          else if (auto *label = qobject_cast<QLabel *>(widget))
            text = label->text();
          else if (auto *line = qobject_cast<QLineEdit *>(widget))
            text = line->text();
          else if (auto *button = qobject_cast<QAbstractButton *>(widget))
            text = button->text();
          if (!text.isEmpty())
            texts.append(QJsonObject{{"class", widget->metaObject()->className()},
                                     {"name", widget->objectName()},
                                     {"text", text}});
        }
        QFile file(output + "/numeric-workspace.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Numeric locale proof unavailable");
        file.write(QJsonDocument(QJsonObject{{"system_locale", QLocale::system().name()},
                                             {"ui_locale", QLocale().name()},
                                             {"comma_value", spin->value()},
                                             {"numbers", numbers},
                                             {"texts", texts},
                                             {"decimal", data["decimal"]},
                                             {"shader_failures", data["shader_failures"]}})
                       .toJson());
        w->grab().save(output + "/numeric-workspace.png");
        timer.stop();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
  }

 public:
  NumericLocaleDemo(QWidget *workspace,
                    QString directory,
                    QString source,
                    std::function<QJsonObject()> snapshot)
      : QObject(workspace),
        w(workspace),
        output(directory),
        mesh(source),
        state(std::move(snapshot))
  {
    w->setProperty("guiProbeOnly", true);
    timer.setInterval(30);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace bts
