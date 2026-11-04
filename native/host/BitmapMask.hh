#pragma once
#include "LayerMask.hh"
#include "BrushAlpha.hh"
#include <QString>
#include <QByteArray>
#include <optional>
#include <set>
class QWidget;
namespace bts {
std::shared_ptr<const AlphaBitmap> read_bitmap_alpha(const QString &path);
std::shared_ptr<const AlphaBitmap> read_bitmap_alpha_bytes(const QByteArray &,const QString &name);
// Raw red-channel data, fitted to one complete UV tile; never color transformed.
LayerMask load_bitmap_mask(const QString &path,int resolution,int tile,bool bilinear);
std::optional<LayerMask> choose_bitmap_mask(QWidget *parent,int resolution,const std::set<int> &tiles);
}
