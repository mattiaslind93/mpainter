#pragma once
#include "TextureExport.hh"
class QWidget;
namespace bts {
class NativeMeshExport;
using ExportMeshSource=std::function<std::shared_ptr<NativeMeshExport>(bool)>;
using ExportSource=std::function<std::vector<ExportTile>(const std::vector<QUuid> &)>;
// Start an owner-context asynchronous capture from this immutable metadata
// request. Completion/failure must run on the dialog's GUI thread, including
// cancellation. The owner retains the shared token through its last callback.
using ExportAsyncSource=std::function<void(const ExportRequest &,std::shared_ptr<const std::atomic_bool>,
    std::function<void(std::vector<ExportTile>)>,std::function<void(std::string)>)>;
bool choose_texture_export(QWidget *,TextureDocument &,const ExportSource &,const ExportMeshSource &mesh_source={},bool displacement_available=false,const ExportAsyncSource &async_source={});
}
