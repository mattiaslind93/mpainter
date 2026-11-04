#pragma once
#include "TextureExport.hh"
#include "MeshBaker.hh"
#include <QObject>
#include <QJsonObject>
#include <memory>
class QMainWindow;
namespace bts {
class NativeMeshExport;
struct AutomationJobHooks {
 std::function<const TextureDocument *()> document;
 std::function<SceneSnapshot()> scene;
 std::function<std::vector<ExportTile>(const std::vector<QUuid> &)> export_source;
 std::function<std::shared_ptr<NativeMeshExport>(bool)> mesh_source;
 std::function<void(const QUuid &,const std::vector<MeshBakeResult> &)> apply_maps;
 std::function<void(const QString &,const QJsonObject &)> event;
};
class AutomationJobs:public QObject {
 public:
 AutomationJobs(QMainWindow *,AutomationJobHooks);
 ~AutomationJobs();
 bool pending()const;
 QJsonValue call(const QString &,const QJsonObject &);
 private:
 struct Impl;std::unique_ptr<Impl> impl_;
};
}
