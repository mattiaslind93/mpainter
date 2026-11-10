// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ViewportDemoCapture.hh"
#include "LayerRowLayout.hh"
#include "LayerBlend.hh"
#include "MaskEffect.hh"
#include "GuiFileSelection.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QDateTime>
#include <QCryptographicHash>
#include <QInputDialog>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QWindow>
#include <functional>
#include <cmath>
namespace bts {
// Explicit offscreen diagnostic: all edits and image loads use production UI routes.
class FolderMaskDemo final : public QObject {
  QWidget *w, *v;
  QString output, mesh, folder, bitmap, paint;
  std::function<QJsonObject()> state;
  QTimer timer;
  std::vector<std::function<void()>> steps;
  QJsonArray captures, checks;
  QJsonObject before, after, paint_expected;
  QJsonArray saved_samples;
  int step = 0, ticks = 0, frame = 0;
  bool busy = false, cold = false;
  QImage cycles_previous;
  qulonglong cycles_revision = 0;
  bool cycles_await = false, cycles_publication_seen = false, expected_refresh_warning = false;
  double cycles_expected = 0;
  QString cycles_sample_status;
  int cycles_done_frame = -1;
  QString failure, smart;
  int mask_effect_count = 0, undo_before = 0;
  qint64 hold_until = 0;
  template<class T> T *get(const char *name) const
  {
    auto *p = w->findChild<T *>(name);
    require(p, name);
    return p;
  }
  static void require(bool okay, const char *why)
  {
    if (!okay)
      throw std::runtime_error(why);
  }
  void action(const char *name)
  {
    auto *a = get<QAction>(name);
    require(a->isEnabled(), name);
    a->trigger();
  }
  QListWidget *list() const
  {
    return get<QListWidget>("layerStack");
  }
  QListWidgetItem *row(const QString &effect = {}) const
  {
    for (int i = 0; i < list()->count(); ++i) {
      auto *item = list()->item(i);
      if (item->data(Qt::UserRole).toString() == folder &&
          item->data(Qt::UserRole + 10).toString() == effect)
        return item;
    }
    throw std::runtime_error("BK target row missing");
  }
  static void click(QWidget *target, QPoint point, Qt::KeyboardModifiers mods = Qt::NoModifier)
  {
    for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
      QMouseEvent event(type,
                        QPointF(point),
                        QPointF(target->mapToGlobal(point)),
                        Qt::LeftButton,
                        type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                        mods);
      QCoreApplication::sendEvent(target, &event);
    }
  }
  void select(bool mask, const QString &effect = {})
  {
    // Selecting content hides mask rows. Reopen the real mask thumbnail before
    // resolving an effect, then reacquire by UUID after the synchronous refresh.
    if (!effect.isEmpty() && !state()["selected_mask"].toBool())
      select(true);
    auto *view = list();
    auto *item = row(effect);
    view->scrollToItem(item);
    const auto index = view->indexFromItem(item);
    const auto rect = view->visualItemRect(item);
    const auto point = effect.isEmpty() ?
                           (mask ?
                                layer_mask_rect(index, rect).center() :
                                QPoint(layer_thumbnail_x(index, rect) + 12, rect.center().y())) :
                           QPoint(rect.left() + item->data(Qt::UserRole + 2).toInt() * 20 + 62,
                                  rect.center().y());
    // No item or index is used after the event, which can rebuild all rows.
    click(view->viewport(), point);
  }
  void capture(const char *stage)
  {
    if (state()["has_mask"].toBool() && !state()["render_ui"].toBool())
      action("guiBkSample");
    auto data = state();
    data["stage"] = stage;
    captures.append(data);
    require(capture_demo_window(w, v).save(output + "/" + stage + ".png"),
            "BK screenshot write failed");
    write();
  }
  void write()
  {
    const auto s = state();
    QJsonObject progress;
    for (const char *key : {"has_document",
                            "render_frames",
                            "composite_pending",
                            "property_pending",
                            "layer_thumbnails_pending",
                            "open_pending",
                            "save_pending",
                            "texture_loading",
                            "material_error",
                            "render"})
      progress[key] = s[key];
    QFile file(output + "/folder-mask.json");
    require(file.open(QIODevice::WriteOnly), "BK proof write failed");
    file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                         {"cold", cold},
                                         {"completed_steps", step},
                                         {"progress", progress},
                                         {"failure", failure},
                                         {"checks", checks},
                                         {"captures", captures}})
                   .toJson());
  }
  void sample(const char *stage, double left, double right)
  {
    action("guiBkSample");
    const auto values = state()["samples"].toArray();
    require(values.size() == 3, "BK GPU packet shape");
    double error = 0;
    for (int i = 0; i < 2; ++i) {
      const auto p = values[i].toObject()["roughness"].toArray();
      require(p.size() == 4 && p[3].toDouble() > .99999, "BK authoritative coverage absent");
      error = std::max(error, std::abs(p[0].toDouble() - (i ? right : left)));
    }
    require(error < 3e-5, "BK independent Folder algebra pixel mismatch");
    checks.append(QJsonObject{{"stage", stage},
                              {"expected", QJsonArray{left, right}},
                              {"actual", values},
                              {"max_error", error},
                              {"tolerance", 3e-5},
                              {"cpu_composite", false}});
    capture(stage);
  }
  static QJsonObject paint_shape(const QJsonObject &payload)
  {
    QJsonArray planes;
    for (const auto &plane : payload["planes"].toArray()) {
      QJsonArray tiles;
      for (const auto &entry : plane.toArray()) {
        auto tile = entry.toObject();
        tile.remove("hashes");
        tiles.append(tile);
      }
      planes.append(tiles);
    }
    return {{"resolution", payload["resolution"]}, {"planes", planes}};
  }
  QJsonObject paint_metadata() const
  {
    for (const auto &value : state()["mask_effects"].toArray()) {
      const auto effect = value.toObject();
      if (effect["type"] != "paint" || QUuid(effect["id"].toString()) != QUuid(paint))
        continue;
      const auto payload = effect["paint"].toObject();
      const auto planes = payload["planes"].toArray();
      require(payload["resolution"].toInt() == 128 && !planes.empty(),
              "BK effect pixel resolution/planes missing");
      bool occupied = false;
      for (const auto &tile : planes[0].toArray()) {
        const auto pages = tile.toObject()["pages"].toArray();
        occupied |= pages.size() == 5 && pages[2].toInt() > 0;
      }
      require(occupied, "BK effect Paint has no scalar pixel page");
      return payload;
    }
    throw std::runtime_error("BK effect Paint payload missing");
  }
  void painted_pixel(const char *stage)
  {
    const auto payload = paint_metadata();
    action("guiBkSample");
    const auto samples = state()["samples"].toArray();
    require(samples.size() == 3, "BK painted native packet shape");
    const auto center = samples[2].toObject()["roughness"].toArray();
    require(center.size() == 4 && std::abs(center[0].toDouble() - .632) < 3e-5 &&
                center[3].toDouble() > .99999,
            "BK direct+effect Paint independent .632 pixel failed");
    checks.append(QJsonObject{
        {"stage", stage},
        {"expected_center", .632},
        {"actual_center", center},
        {"tolerance", 3e-5},
        {"paint_payload", payload},
        {"optional_effect_strokes", state()["effect_strokes"]},
        {"history_scope",
         "Ordinary Add Paint keeps optional effect operation history disabled; actual sparse pixels, exact Undo/Redo and packed native pixels are required. Direct mask retains StrokeArchive."}});
    capture(stage);
  }
  void compare_saved_pixels(const char *stage)
  {
    action("guiBkSample");
    const auto current = state()["samples"].toArray();
    require(current.size() == 3 && saved_samples.size() == 3,
            "BK saved/reopened native packet shape");
    double error = 0;
    for (int i = 0; i < 3; ++i) {
      const auto a = current[i].toObject()["roughness"].toArray(),
                 b = saved_samples[i].toObject()["roughness"].toArray();
      require(a.size() == 4 && b.size() == 4, "BK saved/reopened RGBA shape");
      for (int c = 0; c < 4; ++c)
        error = std::max(error, std::abs(a[c].toDouble() - b[c].toDouble()));
    }
    require(error < 3e-5, "BK saved/reopened final native pixels differ");
    const auto payload = paint_metadata();
    require(paint_shape(payload) == paint_shape(paint_expected),
            "BK effect page shape changed across packed Open");
    checks.append(QJsonObject{
        {"stage", stage},
        {"max_error", error},
        {"tolerance", 3e-5},
        {"saved_pixels", saved_samples},
        {"reopened_pixels", current},
        {"saved_paint_payload", paint_expected},
        {"reopened_paint_payload", payload},
        {"hash_scope",
         "Edit-version hashes can change after GPU-finalized storage. Sparse page shape and actual native pixels are compared."}});
  }
  void load_expected_paint()
  {
    QFile file(qEnvironmentVariable("BTS_BK_PROJECT_DIR") + "/folder-mask.json");
    require(file.open(QIODevice::ReadOnly), "BK warm proof reference missing");
    const auto proof = QJsonDocument::fromJson(file.readAll()).object();
    require(proof["failure"].toString().isEmpty(), "BK warm reference failed");
    for (const auto &value : proof["captures"].toArray()) {
      const auto capture = value.toObject();
      if (capture["stage"] == "saved")
        saved_samples = capture["samples"].toArray();
      if (capture["stage"] == "folder-mask-painted")
        for (const auto &value : capture["mask_effects"].toArray())
          if (value.toObject()["type"] == "paint")
            paint_expected = value.toObject()["paint"].toObject();
    }
    require(!paint_expected.empty() && saved_samples.size() == 3,
            "BK warm pixel references absent");
  }
  void paint_dab()
  {
    const auto input = state();
    checks.append(QJsonObject{{"stage", "native-pointer-dab-input"},
                              {"selected_layer", input["selected_layer"]},
                              {"selected_effect", input["selected_effect"]},
                              {"paint_enabled", input["paint_enabled"]},
                              {"point", input["paint_point"]},
                              {"view", input["view"]},
                              {"brush", input["brush"]},
                              {"input_points", input["input_points"]},
                              {"stroke_batches", input["stroke_batches"]}});
    const auto p = input["paint_point"].toArray();
    require(p.size() == 2, "BK native UV point absent");
    const QPoint point(qRound(p[0].toDouble()), qRound(p[1].toDouble()));
    require(v->rect().contains(point), "BK native UV point outside viewport");
    QMouseEvent hover(QEvent::MouseMove,
                      QPointF(point),
                      QPointF(v->mapToGlobal(point)),
                      Qt::NoButton,
                      Qt::NoButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(v, &hover);
    click(v, point);
    action("flushPointerInput");
    const auto emitted = state();
    checks.append(QJsonObject{{"stage", "native-pointer-dab-emitted"},
                              {"selected_effect", emitted["selected_effect"]},
                              {"input_points", emitted["input_points"]},
                              {"stroke_batches", emitted["stroke_batches"]},
                              {"painting", emitted["painting"]}});
  }
  void file_drop(const QString &path)
  {
    auto *view = list();
    const auto point = view->visualItemRect(row()).center();
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &enter);
    require(enter.isAccepted(), "BK Folder file drag rejected");
    QDropEvent drop(QPointF(point), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &drop);
    require(drop.isAccepted(), "BK Folder file drop rejected");
  }
  void asset_drop(const QString &id)
  {
    auto *view = list();
    const auto point = view->visualItemRect(row()).center();
    QMimeData mime;
    mime.setData("application/x-bts-asset", id.toUtf8());
    QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &enter);
    require(enter.isAccepted(), "BK Folder asset drag rejected");
    QDropEvent drop(QPointF(point), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &drop);
    require(drop.isAccepted(), "BK Folder asset drop rejected");
  }
  void context()
  {
    select(true);
    auto *view = list();
    const auto index = view->indexFromItem(row());
    const auto point = layer_mask_rect(index, view->visualRect(index)).center();
    QMetaObject::invokeMethod(
        view, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, point));
    auto *menu = get<QMenu>("maskContextMenu");
    for (const char *name : {"addWhiteMask",
                             "addBlackMask",
                             "addBitmapMask",
                             "addColorSelectionMask",
                             "applySmartMask",
                             "copyMaskContent",
                             "pasteIntoMask",
                             "createSmartMask",
                             "invertMaskBackground",
                             "removeMask",
                             "addPaintEffect",
                             "addMaskGenerator",
                             "addMaskFillEffect",
                             "addLevelsEffect",
                             "addFilterEffect",
                             "addCompareMask",
                             "addMaskAnchor"})
    {
      bool found = false;
      for (auto *a : menu->actions())
        found |= a->objectName() == name;
      require(found, name);
    }
    menu->close();
    checks.append(
        QJsonObject{{"stage", "shared-folder-mask-menu"}, {"same_actions_as_toolbar", true}});
  }
  void export_files(bool mask)
  {
    busy = true;
    QTimer::singleShot(100, this, [this, mask] {
      try {
        auto *dialog = get<QDialog>("flattenExportDialog");
        const auto path = output + (mask ? "/mask-export" : "/group-export");
        QDir().mkpath(path);
        dialog->findChild<QLineEdit *>("flattenExportDirectory")->setText(path);
        if (mask)
          dialog->findChild<QComboBox *>("flattenMaskFormat")->setCurrentIndex(1);
        dialog->findChild<QPushButton *>("runFlattenExport")->click();
        require(dialog->property("flattenExportError").toString().isEmpty() &&
                    dialog->property("flattenExportFinished").toBool(),
                "BK GPU flattened export failed");
        require(!dialog->property("flattenExportFiles").toStringList().empty(),
                "BK export created no files");
        checks.append(QJsonObject{
            {"stage", mask ? "mask-export" : "group-export"},
            {"files",
             QJsonArray::fromStringList(dialog->property("flattenExportFiles").toStringList())},
            {"probes", dialog->property("flattenExportProbes").toJsonArray()}});
        dialog->accept();
      }
      catch (const std::exception &e) {
        fail(e.what());
      }
    });
    action(mask ? "exportFlattenedMask" : "exportFlattenedGroups");
    busy = false;
  }
  void arm_cycles(double expected)
  {
    cycles_await = true;
    cycles_expected = expected;
    cycles_publication_seen = false;
    cycles_sample_status.clear();
    cycles_done_frame = -1;
    timer.setInterval(20);
  }
  void cycles_capture(const char *stage, double expected)
  {
    require(cycles_await && cycles_publication_seen && !cycles_sample_status.isEmpty() &&
                cycles_done_frame >= 0 && state()["render_frames"].toInt() > cycles_done_frame,
            "BK new publication -> actual Cycles Sample -> Done -> later draw not observed");
    require(state()["render_ui"].toBool() &&
                state()["render"].toObject()["status"].toString().contains("Rendering Done"),
            "BK completed Cycles frame absent");
    action("guiBkSample");
    action("guiBkCyclesSample");
    const auto data = state();
    const auto canonical = data["samples"].toArray();
    require(canonical.size() == 3, "BK Cycles canonical packet shape");
    for (const auto &p : canonical) {
      const auto value = p.toObject()["roughness"].toArray();
      require(value.size() == 4 && std::abs(value[0].toDouble() - expected) < 3e-5 &&
                  value[3].toDouble() > .99999,
              "BK active Cycles canonical channel differs");
    }
    const auto mirror = data["cycles_mirrors"].toObject();
    require(mirror["active"].toBool(), "BK native mirror not active");
    bool color = false, roughness = false;
    qulonglong revision = 0;
    for (const auto &value : mirror["packets"].toArray()) {
      const auto row = value.toObject();
      if (row["role"].toInt() != 0 && row["role"].toInt() != 2)
        continue;
      require(row["current"].toBool(),
              "BK publication packet is not the current wanted/published epoch");
      const auto samples = row["samples"].toArray();
      require(samples.size() == 3, "BK Cycles publication packet shape");
      for (const auto &point : samples) {
        const auto rgba = point.toObject()["rgba"].toArray();
        require(rgba.size() == 4 && std::abs(rgba[0].toDouble() - expected) < 3e-5 &&
                    rgba[3].toDouble() > .99999,
                "BK successful mirror publication packet differs from independent channel");
      }
      if (row["role"].toInt() == 0)
        color = true;
      else {
        roughness = true;
        revision = row["published"].toString().toULongLong();
      }
    }
    require(color && roughness && revision > cycles_revision,
            "BK Cycles publication did not advance Color/Roughness");
    cycles_revision = revision;
    const auto image = capture_viewport_framebuffer(v).convertToFormat(QImage::Format_RGBA8888);
    require(!image.isNull(), "BK completed Cycles framebuffer missing");
    require(image.save(output + "/" + stage + "-viewport.png"), "BK Cycles frame write failed");
    QJsonObject proof{
        {"stage", stage},
        {"expected_channel", expected},
        {"tolerance", 3e-5},
        {"mirror_scope", mirror["scope"]},
        {"mirrors", mirror},
        {"completed_render", data["render"]},
        {"observed_sample_status", cycles_sample_status},
        {"done_observed_frame", cycles_done_frame},
        {"captured_frame", data["render_frames"]},
        {"frame_sha256",
         QString::fromLatin1(QCryptographicHash::hash(
                                 QByteArrayView(reinterpret_cast<const char *>(image.constBits()),
                                                image.sizeInBytes()),
                                 QCryptographicHash::Sha256)
                                 .toHex())},
        {"display_scope",
         "Same Cycles session, camera, geometry, lighting and output. Completed frames only; no cross-engine RGB equality."}};
    if (!cycles_previous.isNull()) {
      require(image.size() == cycles_previous.size(), "BK Cycles edit changed viewport extent");
      const QRect roi(image.width() * 35 / 100,
                      image.height() * 35 / 100,
                      image.width() * 30 / 100,
                      image.height() * 30 / 100);
      double error = 0;
      int changed = 0;
      for (int y = roi.top(); y <= roi.bottom(); ++y)
        for (int x = roi.left(); x <= roi.right(); ++x) {
          const auto *a = image.constScanLine(y) + x * 4,
                     *b = cycles_previous.constScanLine(y) + x * 4;
          int total = 0;
          for (int c = 0; c < 3; ++c)
            total += std::abs(int(a[c]) - int(b[c]));
          error += total / (3. * 255);
          changed += total > 3;
        }
      error /= roi.width() * roi.height();
      require(changed > 64 && error > .002,
              "BK mask edit did not visibly alter a completed Cycles frame");
      proof["roi"] = QJsonArray{roi.x(), roi.y(), roi.width(), roi.height()};
      proof["changed_pixels"] = changed;
      proof["mean_absolute_rgb"] = error;
    }
    cycles_previous = image;
    checks.append(proof);
    capture(stage);
    cycles_await = false;
    timer.setInterval(160);
  }
  void fail(const char *why)
  {
    failure = QString::fromUtf8(why);
    timer.stop();
    try {
      auto diagnostic = state();
      diagnostic["stage"] = "failure";
      captures.append(diagnostic);
      write();
    }
    catch (...) {
    }
    std::fprintf(stderr, "BTS_BK_FOLDER_MASK_FAIL step=%d %s\n", step, why);
    QCoreApplication::exit(1);
  }
  void advance()
  {
    if (busy || QDateTime::currentMSecsSinceEpoch() < hold_until)
      return;
    try {
      if (++ticks > 12000)
        throw std::runtime_error("BK GUI timed out");
      if (!w->isActiveWindow() && !QApplication::activeModalWidget() &&
          !QApplication::activePopupWidget())
      {
        w->raise();
        w->activateWindow();
        w->windowHandle()->requestActivate();
        return;
      }
      v->update();
      const auto s = state();
      if (s["render_ui"].toBool()) {
        const auto render = s["render"].toObject();
        require(!render["image_mirror_paused"].toBool(), "BK Cycles mirror failed");
        const auto status = render["status"].toString();
        require(!status.contains("error", Qt::CaseInsensitive), "BK Cycles render error");
        if (cycles_await) {
          action("guiBkCyclesSample");
          const auto mirror = state()["cycles_mirrors"].toObject();
          for (const auto &value : mirror["packets"].toArray()) {
            const auto row = value.toObject();
            if (row["role"].toInt() != 2 || !row["current"].toBool() ||
                row["published"].toString().toULongLong() <= cycles_revision)
              continue;
            const auto points = row["samples"].toArray();
            bool matches = points.size() == 3;
            for (const auto &point : points)
              matches &= std::abs(point.toObject()["rgba"].toArray()[0].toDouble() -
                                  cycles_expected) < 3e-5;
            cycles_publication_seen |= matches;
          }
          // Only renderer Sample N/M counts. The host's Preparing wrapper can hide
          // stale Done text and therefore cannot establish a render restart.
          static const QRegularExpression sample("\\bSample [0-9]+/[0-9]+");
          if (cycles_publication_seen && sample.match(status).hasMatch())
            cycles_sample_status = status;
          if (cycles_sample_status.isEmpty() || !status.contains("Rendering Done"))
            return;
          if (cycles_done_frame < 0) {
            cycles_done_frame = s["render_frames"].toInt();
            v->update();
            return;
          }
          if (s["render_frames"].toInt() <= cycles_done_frame)
            return;
        }
        else if (!status.contains("Rendering Done"))
          return;
      }
      if ((cold || step != 1) &&
          (s["smart_mask_saving"].toBool() || s["open_pending"].toBool() ||
           s["save_pending"].toBool() || s["texture_loading"].toBool() ||
           s["composite_pending"].toBool() || s["property_pending"].toBool() ||
           s["layer_thumbnails_pending"].toBool() || s["render_frames"].toInt() < frame))
        return;
      for (const char *key : {"material_error", "open_error", "save_error"})
        if (!(expected_refresh_warning && QString::fromLatin1(key) == "material_error"))
          require(s[key].toString().isEmpty(), qPrintable(s[key].toString()));
      if (step == int(steps.size())) {
        timer.stop();
        write();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        return;
      }
      busy = true;
      steps[step++]();
      busy = false;
      frame = state()["render_frames"].toInt() + 3;
      write();
    }
    catch (const std::exception &e) {
      fail(e.what());
    }
  }

  void warm_steps()
  {
    steps.push_back([this] { action("newProject"); });
    steps.push_back([this] {
      get<QComboBox>("projectTemplate")->setCurrentIndex(0);
      get<QLineEdit>("meshPath")->setText(mesh);
      auto *size = get<QComboBox>("documentResolution");
      size->setCurrentIndex(size->findData(128));
      get<QPushButton>("createProject")->click();
    });
    steps.push_back([this] {
      action("guiBkPrepare");
      folder = state()["selected_layer"].toString();
      get<QComboBox>("layerChannelContext")
          ->setCurrentIndex(get<QComboBox>("layerChannelContext")->findData(2));
    });
    steps.push_back([this] {
      action("addBitmapMask");
      bitmap = state()["selected_effect"].toString();
      require(state()["selected_mask"].toBool() && !bitmap.isEmpty(),
              "BK bitmap creation did not select mask Fill");
      require(get<QWidget>("maskFillProperties")->isVisible(), "BK bitmap panel absent");
      get<QDoubleSpinBox>("maskEffectFillValue")->setValue(.25);
    });
    steps.push_back([this] {
      sample("passthrough-quarter", .74, .74);
      select(false);
      auto *blend = get<QComboBox>("folderBlending");
      blend->setCurrentIndex(blend->findData(int(LayerBlend::Normal)));
    });
    steps.push_back([this] {
      sample("normal-quarter", .755, .755);
      get<QComboBox>("folderBlending")
          ->setCurrentIndex(
              get<QComboBox>("folderBlending")->findData(int(LayerBlend::Passthrough)));
      select(true, bitmap);
      undo_before = state()["undo_count"].toInt();
      expected_refresh_warning = true;
      w->setProperty("bkTextureRefreshFailure", true);
      select_gui_test_file(w, "fillTextureFileDialog", qEnvironmentVariable("BTS_BK_BITMAP"));
      get<QComboBox>("maskFillResource")->setCurrentIndex(2);
    });
    steps.push_back([this] {
      const auto s = state();
      require(s["material_published"].toBool() &&
                  s["material_error"].toString() ==
                      "Injected BK postcommit texture refresh failure",
              "BK expected committed refresh failure not observed");
      require(s["undo_count"].toInt() == undo_before + 1,
              "BK committed refresh failure lost Undo");
      bool retained = false;
      for (const auto &asset : s["project_resources"].toArray())
        retained |= asset.toObject()["name"].toString() ==
                    QFileInfo(qEnvironmentVariable("BTS_BK_BITMAP")).completeBaseName();
      require(retained, "BK postcommit refresh failure rolled back retained texture");
      checks.append(QJsonObject{{"stage", "postcommit-refresh-resource-retained"},
                                {"undo_retained", true},
                                {"resource_retained", true},
                                {"warning", s["material_error"]}});
      action("guiBkAcknowledgeRefreshWarning");
      expected_refresh_warning = false;
    });
    steps.push_back([this] {
      sample("bitmap-file", .8 - .24 * (64. / 255), .8 - .24 * (192. / 255));
      context();
      action("guiBkSaveBitmap");
    });
    steps.push_back([this] {
      select(true);
      action("removeMask");
      select(false);
      before = state()["document"].toObject();
      undo_before = state()["undo_count"].toInt();
      file_drop(qEnvironmentVariable("BTS_BK_BITMAP"));
      get<QPushButton>("cancelTextureLoads")->click();
    });
    steps.push_back([this] {
      require(state()["document"].toObject() == before &&
                  state()["undo_count"].toInt() == undo_before,
              "BK cancelled file drop mutated document/Undo");
      checks.append(QJsonObject{{"stage", "cancelled-folder-load"},
                                {"document_unchanged", true},
                                {"undo_unchanged", true}});
      file_drop(qEnvironmentVariable("BTS_BK_BITMAP"));
    });
    steps.push_back([this] {
      sample("folder-file-drop-autocreate", .8 - .24 * (64. / 255), .8 - .24 * (192. / 255));
      require(state()["undo_count"].toInt() == undo_before + 1,
              "BK drop did not create exactly one Undo");
      after = state()["document"].toObject();
      action("undoDocument");
    });
    steps.push_back([this] {
      require(state()["document"].toObject() == before,
              "BK drop did not undo exact mask+Fill tree");
      action("redoDocument");
    });
    steps.push_back([this] {
      require(state()["document"].toObject() == after, "BK drop redo differs");
      select(false);
      get<QLineEdit>("shelfSearch")
          ->setText("n:\"" + QFileInfo(qEnvironmentVariable("BTS_BK_BITMAP")).completeBaseName() +
                    "\"=");
    });
    steps.push_back([this] {
      auto *shelf = get<QListWidget>("shelfAssets");
      require(shelf->count() >= 1, "BK project bitmap Shelf card absent");
      mask_effect_count = state()["mask_effects"].toArray().size();
      asset_drop(shelf->item(0)->data(Qt::UserRole).toString());
    });
    steps.push_back([this] {
      require(state()["selected_mask"].toBool() &&
                  state()["mask_effects"].toArray().size() == mask_effect_count + 1,
              "BK texture Folder row drop failed mask Fill creation");
      sample("folder-texture-shelf-drop", .8 - .24 * (64. / 255), .8 - .24 * (192. / 255));
      action("undoDocument");
    });
    steps.push_back([this] {
      select(true);
      QTimer::singleShot(100, this, [this] {
        auto *dialog = get<QInputDialog>("smartMaskNameDialog");
        dialog->setTextValue("BK private bitmap mask");
        dialog->accept();
      });
      action("createSmartMask");
    });
    steps.push_back([this] {
      action("applySmartMask");
      get<QLineEdit>("shelfSearch")->setText("n:\"BK private bitmap mask\"=");
    });
    steps.push_back([this] {
      auto *shelf = get<QListWidget>("shelfAssets");
      require(shelf->count() == 1, "BK own Mask Generator Shelf card absent");
      smart = shelf->item(0)->data(Qt::UserRole).toString();
      mask_effect_count = state()["mask_effects"].toArray().size();
      click(shelf->viewport(), shelf->visualItemRect(shelf->item(0)).center());
      hold_until = QDateTime::currentMSecsSinceEpoch() + QApplication::doubleClickInterval() + 100;
    });
    steps.push_back([this] {
      require(state()["mask_effects"].toArray().size() == mask_effect_count + 1,
              "BK Mask Generator single click did not apply once");
      capture("smart-mask-click");
      action("undoDocument");
    });
    steps.push_back([this] {
      auto *shelf = get<QListWidget>("shelfAssets");
      mask_effect_count = state()["mask_effects"].toArray().size();
      const auto point = shelf->visualItemRect(shelf->item(0)).center();
      QMouseEvent event(QEvent::MouseButtonDblClick,
                        QPointF(point),
                        QPointF(shelf->viewport()->mapToGlobal(point)),
                        Qt::LeftButton,
                        Qt::LeftButton,
                        Qt::NoModifier);
      QCoreApplication::sendEvent(shelf->viewport(), &event);
      QMouseEvent release(QEvent::MouseButtonRelease,
                          QPointF(point),
                          QPointF(shelf->viewport()->mapToGlobal(point)),
                          Qt::LeftButton,
                          Qt::NoButton,
                          Qt::NoModifier);
      QCoreApplication::sendEvent(shelf->viewport(), &release);
      hold_until = QDateTime::currentMSecsSinceEpoch() + QApplication::doubleClickInterval() + 100;
    });
    steps.push_back([this] {
      require(state()["mask_effects"].toArray().size() == mask_effect_count + 1,
              "BK Mask Generator double click duplicated or missed apply");
      action("undoDocument");
    });
    steps.push_back([this] {
      auto *shelf = get<QListWidget>("shelfAssets");
      const auto point = shelf->visualItemRect(shelf->item(0)).center();
      QMetaObject::invokeMethod(
          shelf, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, point));
      action("applyShelfSmartMask");
      get<QMenu>("presetResourceContextMenu")->close();
    });
    steps.push_back([this] {
      require(state()["mask_effects"].toArray().size() == mask_effect_count + 1,
              "BK Mask Generator context apply missed");
      action("undoDocument");
      select(false);
      asset_drop(smart);
    });
    steps.push_back([this] {
      require(state()["selected_mask"].toBool() &&
                  state()["mask_effects"].toArray().size() == mask_effect_count + 1,
              "BK Mask Generator Folder drop missed");
      capture("smart-mask-folder-drop");
      action("undoDocument");
    });
    steps.push_back([this] {
      select(true);
      before = state()["document"].toObject();
      action("addCompareMask");
    });
    steps.push_back([this] {
      require(get<QWidget>("compareMaskProperties")->isVisible(), "BK Compare panel absent");
      capture("folder-compare");
      action("undoDocument");
    });
    steps.push_back([this] {
      require(state()["document"].toObject() == before, "BK Compare Undo changed source mask");
      select(true);
      action("addBlackMask");
      get<QDoubleSpinBox>("maskGrayscale")->setValue(1);
      paint_dab();
    });
    steps.push_back([this] {
      require(state()["mask_strokes"].toInt() > 0, "BK direct Folder mask Paint stroke missing");
      action("addPaintEffect");
      paint = state()["selected_effect"].toString();
      require(state()["paint_enabled"].toBool(), "BK Folder mask Paint effect target disabled");
      get<QDoubleSpinBox>("paintEffectOpacity")->setValue(50);
      get<QDoubleSpinBox>("maskGrayscale")->setValue(.4);
    });
    steps.push_back([this] {
      before = state()["document"].toObject();
      paint_dab();
    });
    steps.push_back([this] {
      painted_pixel("folder-mask-painted");
      after = state()["document"].toObject();
      action("undoDocument");
    });
    steps.push_back([this] {
      require(state()["document"].toObject() == before, "BK effect stroke Undo tree differs");
      action("redoDocument");
    });
    steps.push_back([this] {
      require(state()["document"].toObject() == after, "BK effect stroke Redo tree differs");
      paint_expected = paint_metadata();
      painted_pixel("folder-paint-redone");
      action("guiBkSavePaint");
    });
    steps.push_back([this] {
      select(true);
      context();
      action("copyMaskContent");
      action("pasteIntoMask");
    });
    steps.push_back([this] {
      capture("pasted-mask");
      action("undoDocument");
    });
    steps.push_back([this] {
      select(true);
      action("addMaskFillEffect");
      bitmap = state()["selected_effect"].toString();
      get<QDoubleSpinBox>("maskEffectFillValue")->setValue(.25);
      get<QComboBox>("maskFillProjection")->setCurrentIndex(1);
    });
    steps.push_back([this] {
      require(get<QComboBox>("maskFillProjection")->currentIndex() == 1,
              "BK triplanar UI edit lost");
      get<QComboBox>("maskFillProjection")->setCurrentIndex(0);
      select_gui_test_file(w, "fillTextureFileDialog", qEnvironmentVariable("BTS_BK_BITMAP"));
      get<QComboBox>("maskFillResource")->setCurrentIndex(2);
    });
    steps.push_back([this] {
      action("addLevelsEffect");
      get<QDoubleSpinBox>("levelsGamma")->setValue(1.7);
    });
    steps.push_back([this] {
      capture("folder-levels");
      action("addFilterEffect");
      action("filterBlur");
    });
    steps.push_back([this] {
      require(get<QWidget>("filterProperties")->isVisible(), "BK Filter panel absent");
      capture("folder-blur");
      action("filterSharpen");
    });
    steps.push_back([this] {
      capture("folder-sharpen");
      action("filterWarp");
    });
    steps.push_back([this] {
      capture("folder-warp");
      action("addMaskGenerator");
    });
    steps.push_back([this] {
      require(get<QWidget>("maskGeneratorProperties")->isVisible(), "BK Generator panel absent");
      action("generatorNativeGrunge");
      capture("folder-native-generator");
      action("addColorSelectionMask");
    });
    steps.push_back([this] {
      require(get<QWidget>("colorSelectionProperties")->isVisible(),
              "BK color selection panel absent");
      action("addMaskAnchor");
    });
    steps.push_back([this] {
      require(get<QWidget>("anchorProperties")->isVisible(), "BK Anchor panel absent");
      capture("all-folder-mask-effects");
      select(true);
      action("guiBkSave");
    });
    steps.push_back([this] {
      capture("saved");
      saved_samples = state()["samples"].toArray();
      action("guiBkOpen");
    });
    steps.push_back([this] {
      folder = state()["selected_layer"].toString();
      select(true);
      action("guiBkUv");
      capture("reopened");
      action("guiBkSample");
      require(state()["mask_strokes"].toInt() > 0, "BK reopened direct Paint history missing");
      bool painted = false;
      for (const auto &e : state()["mask_effects"].toArray())
        painted |= e.toObject()["type"].toString() == "paint";
      require(painted, "BK reopened Paint effect missing");
      select(true, paint);
    });
    steps.push_back([this] {
      compare_saved_pixels("reopened-final-stack");
      select(true);
      auto *view = list();
      const auto index = view->indexFromItem(row());
      click(view->viewport(),
            layer_mask_rect(index, view->visualRect(index)).center(),
            Qt::AltModifier);
    });
    steps.push_back([this] {
      require(state()["mask_isolated"].toBool(), "BK Alt-click mask isolation failed");
      capture("folder-mask-isolation");
      v->setFocus(Qt::OtherFocusReason);
      for (const auto type : {QEvent::KeyPress, QEvent::KeyRelease}) {
        QKeyEvent key(type, Qt::Key_M, Qt::NoModifier);
        QCoreApplication::sendEvent(v, &key);
      }
    });
    steps.push_back([this] {
      require(state()["inspection"].toInt() == -1 && !state()["mask_isolated"].toBool(),
              "BK focused M did not restore Material inspection");
      checks.append(QJsonObject{{"stage", "mask-isolation-material-restored"},
                                {"inspection", state()["inspection"]}});
      select(false);
      export_files(false);
    });
    steps.push_back([this] {
      select(true);
      export_files(true);
    });
    steps.push_back([this] {
      select(true);
      action("addBlackMask");
      action("addMaskFillEffect");
      bitmap = state()["selected_effect"].toString();
      get<QDoubleSpinBox>("maskEffectFillValue")->setValue(.25);
    });
    steps.push_back([this] { get<QDoubleSpinBox>("maskEffectFillValue")->setValue(.75); });
    steps.push_back([this] {
      require(state()["inspection"].toInt() == -1, "BK Material inspection must precede Cycles");
      arm_cycles(.62);
      action("renderingMode");
      get<QSpinBox>("renderSamples")->setValue(1024);
      get<QCheckBox>("renderDenoise")->setChecked(false);
    });
    steps.push_back([this] {
      cycles_capture("cycles-mask-three-quarters", .62);
      arm_cycles(.74);
      action("undoDocument");
    });
    steps.push_back([this] {
      cycles_capture("cycles-mask-undo-quarter", .74);
      arm_cycles(.62);
      action("redoDocument");
    });
    steps.push_back([this] {
      cycles_capture("cycles-mask-redo-three-quarters", .62);
      action("paintingMode");
      select(false);
      action("flattenLayer");
    });
    steps.push_back([this] {
      capture("flattened");
      action("undoDocument");
    });
    steps.push_back([this] {
      capture("flatten-undone");
      action("redoDocument");
    });
    steps.push_back([this] { capture("flatten-redone"); });
  }
  void cold_steps()
  {
    steps.push_back([this] {
      load_expected_paint();
      action("guiBkOpenBitmap");
    });
    steps.push_back([this] {
      folder = state()["selected_layer"].toString();
      sample("source-cache-absent-bitmap", .8 - .24 * (64. / 255), .8 - .24 * (192. / 255));
      action("guiBkOpenPaint");
    });
    steps.push_back([this] {
      folder = state()["selected_layer"].toString();
      select(true);
      require(state()["mask_strokes"].toInt() > 0, "BK cold direct mask StrokeArchive missing");
      for (const auto &e : state()["mask_effects"].toArray())
        if (e.toObject()["type"] == "paint")
          paint = QUuid(e.toObject()["id"].toString()).toString();
      select(true, paint);
      painted_pixel("source-cache-absent-painted");
      require(paint_shape(paint_metadata()) == paint_shape(paint_expected),
              "BK cold paint-only sparse payload shape changed");
      action("guiBkOpen");
    });
    steps.push_back([this] {
      folder = state()["selected_layer"].toString();
      select(true);
      require(state()["mask_strokes"].toInt() > 0, "BK cold direct Paint stroke archive missing");
      for (const auto &e : state()["mask_effects"].toArray())
        if (e.toObject()["type"].toString() == "paint")
          paint = QUuid(e.toObject()["id"].toString()).toString();
      require(!paint.isEmpty(), "BK cold Paint effect absent");
      select(true, paint);
    });
    steps.push_back([this] {
      compare_saved_pixels("source-cache-absent-final-stack");
      capture("source-cache-absent-strokes-and-pixels");
    });
  }

 public:
  FolderMaskDemo(QWidget *window,
                 QWidget *viewport,
                 QString directory,
                 QString source,
                 std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        v(viewport),
        output(std::move(directory)),
        mesh(std::move(source)),
        state(std::move(snapshot)),
        cold(qEnvironmentVariableIsSet("BTS_BK_COLD"))
  {
    QDir().mkpath(output);
    if (cold)
      cold_steps();
    else
      warm_steps();
    timer.setInterval(160);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace bts
