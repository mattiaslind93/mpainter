#pragma once
#include "ViewportClipping.hh"
#include <QString>
#include <functional>
class QWidget;
namespace bts {
ViewportClipping load_viewport_clipping();
// Startup/toggle only: navigation events never perform settings I/O.
bool load_trackball_navigation();
void save_trackball_navigation(bool enabled);
struct ReferenceImagePreferences {
  bool enabled = false;
  QString path;
  double center_x = 0.5;
  double center_y = 0.5;
  double vertical_extent = 1.15;
};
ReferenceImagePreferences load_reference_image_preferences();
void save_reference_image_preferences(const ReferenceImagePreferences &preferences);
void show_preferences(QWidget *parent,const QString &shelf,bool project_open,ViewportClipping current,std::function<void(ViewportClipping)> preview);
}
