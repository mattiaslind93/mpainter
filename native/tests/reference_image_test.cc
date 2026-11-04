#include "ColorPicker.hh"
#include "Preferences.hh"
#include "ReferenceImage.hh"
#include "TextureDocument.hh"

#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace bts;

namespace {

bool close_color(const PickerColor &a, const PickerColor &b, float eps = 0.015f) {
  return std::abs(a[0] - b[0]) <= eps &&
         std::abs(a[1] - b[1]) <= eps &&
         std::abs(a[2] - b[2]) <= eps &&
         std::abs(a[3] - b[3]) <= eps;
}

}  // namespace

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QSettings::setDefaultFormat(QSettings::IniFormat);

  QTemporaryDir temp;
  assert(temp.isValid());
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());

  // 1. Aspect-ratio fitting and screen <-> UV coordinate mapping.
  {
    const QRectF pane(600.0, 0.0, 400.0, 400.0);
    // Wide 2:1 image in square pane needs larger vertical extent so horizontal fits.
    const double fit_wide = reference_image_fit_vertical_extent(QSize(400, 400), 200, 100);
    assert(fit_wide > 2.0);
    const QRectF rect_wide = reference_image_screen_rect(pane, 200, 100, QPointF(0.5, 0.5), fit_wide);
    assert(std::abs(rect_wide.width() / rect_wide.height() - 2.0) < 1e-4);
    assert(rect_wide.width() <= pane.width());

    // Center of pane maps to (0.5, 0.5) UV.
    const auto center_uv = reference_image_uv_at(pane, 200, 100, QPointF(0.5, 0.5), fit_wide, pane.center());
    assert(center_uv.has_value());
    assert(std::abs(center_uv->x() - 0.5) < 1e-4);
    assert(std::abs(center_uv->y() - 0.5) < 1e-4);

    // Top-left inside rect maps to (0, 1) UV, bottom-right inside rect maps to (1, 0) UV.
    const auto tl_uv = reference_image_uv_at(
        pane, 200, 100, QPointF(0.5, 0.5), fit_wide, QPointF(rect_wide.left() + 0.5, rect_wide.top() + 0.5));
    assert(tl_uv.has_value());
    assert(tl_uv->x() >= 0.0 && tl_uv->x() < 0.05);
    assert(tl_uv->y() <= 1.0 && tl_uv->y() > 0.95);

    // Points outside the image rect or outside the pane return std::nullopt.
    assert(!reference_image_uv_at(pane, 200, 100, QPointF(0.5, 0.5), fit_wide, QPointF(pane.left() + 5.0, pane.top() + 5.0)).has_value());
    assert(!reference_image_uv_at(pane, 200, 100, QPointF(0.5, 0.5), fit_wide, QPointF(100.0, 200.0)).has_value());
  }

  // 2. Reference image loading from disk and linear RGBA color picking.
  const QString ref_path = temp.filePath("reference_palette.png");
  const QColor q_red(220, 40, 30, 255);
  const QColor q_green(35, 190, 65, 255);
  const QColor q_blue(25, 80, 230, 255);
  const QColor q_gold(240, 195, 45, 255);
  {
    QImage img(64, 32, QImage::Format_RGBA8888);
    for (int y = 0; y < 32; ++y) {
      for (int x = 0; x < 64; ++x) {
        const QColor c = (y < 16) ? ((x < 32) ? q_red : q_green) : ((x < 32) ? q_blue : q_gold);
        img.setPixelColor(x, y, c);
      }
    }
    assert(img.save(ref_path));
  }

  const LoadedReferenceImage loaded = load_reference_image(ref_path);
  assert(loaded.valid());
  assert(loaded.width == 64 && loaded.height == 32);

  // Top-left quadrant: x=0.25, y=0.75 (UV y=1 is top) -> red
  const auto sample_tl = sample_reference_image_uv_rgba(loaded, 0.25, 0.75);
  assert(sample_tl.has_value());
  assert(close_color(*sample_tl, picker_from_srgb(q_red)));

  // Top-right quadrant: x=0.75, y=0.75 -> green
  const auto sample_tr = sample_reference_image_uv_rgba(loaded, 0.75, 0.75);
  assert(sample_tr.has_value());
  assert(close_color(*sample_tr, picker_from_srgb(q_green)));

  // Bottom-left quadrant: x=0.25, y=0.25 -> blue
  const auto sample_bl = sample_reference_image_uv_rgba(loaded, 0.25, 0.25);
  assert(sample_bl.has_value());
  assert(close_color(*sample_bl, picker_from_srgb(q_blue)));

  // Bottom-right quadrant: x=0.75, y=0.25 -> gold
  const auto sample_br = sample_reference_image_uv_rgba(loaded, 0.75, 0.25);
  assert(sample_br.has_value());
  assert(close_color(*sample_br, picker_from_srgb(q_gold)));

  // Out-of-bounds UV returns nullopt.
  assert(!sample_reference_image_uv_rgba(loaded, -0.01, 0.5).has_value());
  assert(!sample_reference_image_uv_rgba(loaded, 1.01, 0.5).has_value());
  assert(!sample_reference_image_uv_rgba(loaded, 0.5, -0.01).has_value());
  assert(!sample_reference_image_uv_rgba(loaded, 0.5, 1.01).has_value());

  // 3. ColorPicker::accept_sample updates active brush/fill color.
  {
    ColorPicker picker;
    int started_count = 0;
    int finished_count = 0;
    PickerColor received{0.0f, 0.0f, 0.0f, 1.0f};
    picker.started = [&] { ++started_count; };
    picker.changed = [&](const PickerColor &c) { received = c; };
    picker.finished = [&] { ++finished_count; };

    picker.accept_sample(*sample_tr);
    assert(started_count == 1);
    assert(finished_count == 1);
    assert(close_color(received, *sample_tr, 1e-5f));
    assert(close_color(picker.color(), *sample_tr, 1e-5f));
  }

  // 4. Preferences persistence.
  {
    ReferenceImagePreferences prefs;
    prefs.enabled = true;
    prefs.path = ref_path;
    save_reference_image_preferences(prefs);

    const ReferenceImagePreferences reloaded = load_reference_image_preferences();
    assert(reloaded.enabled == true);
    assert(reloaded.path == ref_path);
  }

  // 5. TextureDocument (.btsproj) persistence and edit_state exclusion.
  {
    SceneSnapshot scene;
    MaterialSummary material;
    material.session_id = 1;
    material.name = "DefaultMaterial";
    material.uniform_editable = true;
    scene.materials.push_back(material);
    ImportSummary imported;
    imported.materials = {{1, "DefaultMaterial"}};
    NewProjectOptions options;
    options.resolution = DocumentResolution::Pixels128;

    TextureDocument document(options, imported, scene);
    const QJsonObject edit_before = document.edit_state();

    ReferenceImageState ref_state;
    ref_state.enabled = true;
    ref_state.path = ref_path.toStdString();
    ref_state.center = {0.35, 0.65};
    ref_state.vertical_extent = 1.85;
    set_document_reference_image(document, ref_state);

    // Reference image view state does not pollute Undo/Redo layer edit_state comparisons.
    assert(document.edit_state() == edit_before);

    const auto &set = document.set(1);
    const std::array<TextureSetBinding, 1> bindings{{{set.id, 1, set.name}}};
    const QJsonObject json = document.to_json();
    assert(json.contains("reference_image"));

    const TextureDocument reopened = TextureDocument::from_json(json, bindings);
    assert(document_reference_image(reopened) == ref_state);
  }

  std::puts("Reference image 2D view, aspect fit, color picking, preferences, and .btsproj persistence PASS");
  return 0;
}
