// SPDX-License-Identifier: GPL-3.0-or-later
#include "MeshExportDialog.hh"
#include "EditTiming.hh"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include "FileDialogs.hh"
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QTimer>
#include <atomic>
#include <thread>
namespace bts {
namespace {
class ExportWait final : public QDialog {
 public:
  using QDialog::QDialog;
  void reject() override {}
};
}  // namespace
void choose_mesh_export(
    QWidget *parent,
    const QString &mesh_path,
    bool displacement_available,
    const std::function<std::shared_ptr<NativeMeshExport>(bool, bool)> &prepare)
{
  QDialog dialog(parent);
  dialog.setObjectName("meshExportDialog");
  dialog.setWindowTitle("Export Mesh");
  dialog.resize(500, 250);
  auto *layout = new QVBoxLayout(&dialog);
  auto *base = new QRadioButton("Without displacement/tessellation", &dialog);
  base->setObjectName("meshExportBase");
  base->setChecked(true);
  layout->addWidget(base);
  auto *triangulate = new QCheckBox("Apply triangulation", &dialog);
  triangulate->setObjectName("meshExportTriangulate");
  triangulate->setChecked(true);
  triangulate->setToolTip(
      "Use Blender's native mesh triangulation to avoid downstream triangulation differences.");
  layout->addWidget(triangulate);
  auto *displaced = new QRadioButton("With displacement/tessellation", &dialog);
  displaced->setObjectName("meshExportDisplaced");
  displaced->setEnabled(displacement_available);
  displaced->setToolTip(
      displacement_available ?
          "Export the active preview subdivision and signed Displacement channel. Bump does not move geometry." :
          "Enable a nonzero Displacement preview with channel content to export displaced geometry.");
  layout->addWidget(displaced);
  auto *normals = new QCheckBox("Recompute vertex normals", &dialog);
  normals->setObjectName("meshExportRecomputeNormals");
  normals->setToolTip(
      "Recompute shading normals after displacement; switch off to retain the preview mesh normals for a normal-map workflow.");
  normals->setChecked(true);
  normals->setEnabled(false);
  layout->addWidget(normals);
  QObject::connect(displaced, &QRadioButton::toggled, &dialog, [&](bool enabled) {
    normals->setEnabled(enabled);
    triangulate->setEnabled(!enabled);
  });
  auto *status = new QLabel(&dialog);
  status->setObjectName("meshExportStatus");
  status->setWordWrap(true);
  layout->addWidget(status);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
  auto *run = buttons->addButton("Export", QDialogButtonBox::AcceptRole);
  run->setObjectName("runMeshExport");
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(run, &QPushButton::clicked, &dialog, [&] {
    bts::FileDialog picker(&dialog,
                           "Export mesh",
                           QFileInfo(mesh_path).absolutePath() + "/" +
                               QFileInfo(mesh_path).completeBaseName() + ".obj",
                           "Wavefront OBJ (*.obj);;USD binary (*.usdc);;USD package (*.usdz)");
    picker.setObjectName("meshExportPicker");
    picker.setAcceptMode(QFileDialog::AcceptSave);
    picker.setDefaultSuffix("obj");
    picker.setOption(QFileDialog::DontUseNativeDialog);
    if (picker.exec() != QDialog::Accepted || picker.selectedFiles().isEmpty())
      return;
    try {
      std::shared_ptr<NativeMeshExport> job;
      {
        performance::Scope timing("mesh_export_snapshot");
        job = prepare(displaced->isChecked(), normals->isChecked());
      }
      dialog.setProperty("meshExportSource", job->summary());
      run->setEnabled(false);
      buttons->button(QDialogButtonBox::Cancel)->setEnabled(false);
      triangulate->setEnabled(false);
      status->setText("Exporting mesh…");
      std::atomic_bool done = false;
      std::string error;
      const auto path = picker.selectedFiles().front().toStdString();
      const bool triangles = displaced->isChecked() || triangulate->isChecked();
      std::jthread worker([&] {
        try {
          EditTiming timing("BTS_MESH_EXPORT_MS");
          job->write(path, triangles);
        }
        catch (const std::exception &e) {
          error = e.what();
        }
        done.store(true, std::memory_order_release);
      });
      ExportWait wait(&dialog);
      wait.setWindowTitle("Export Mesh");
      auto *wait_layout = new QVBoxLayout(&wait);
      wait_layout->addWidget(new QLabel("Exporting mesh…", &wait));
      wait.setWindowFlag(Qt::WindowCloseButtonHint, false);
      QTimer poll;
      poll.setInterval(10);
      QObject::connect(&poll, &QTimer::timeout, &wait, [&] {
        if (done.load(std::memory_order_acquire))
          wait.accept();
      });
      poll.start();
      wait.exec();
      worker.join();
      run->setEnabled(true);
      buttons->button(QDialogButtonBox::Cancel)->setEnabled(true);
      triangulate->setEnabled(!displaced->isChecked());
      if (!error.empty())
        throw std::runtime_error(error);
      status->setText("Mesh export successfully finished");
      dialog.setProperty("meshExportFinished", true);
      dialog.setProperty("meshExportPath", QString::fromStdString(path));
    }
    catch (const std::exception &e) {
      status->setText("Mesh export failed: " + QString::fromUtf8(e.what()));
      dialog.setProperty("meshExportError", QString::fromUtf8(e.what()));
    }
  });
  dialog.exec();
}
}  // namespace bts
