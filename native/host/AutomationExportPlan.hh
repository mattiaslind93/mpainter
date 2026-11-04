#pragma once
#include "ExportConfiguration.hh"
namespace bts {
struct AutomationExportPlan {ExportRequest request;std::vector<QUuid> selected;bool usd=false,mesh_displacement=false;};
// Pure filename/configuration planning. No material evaluation, file writes or GPU access.
AutomationExportPlan automation_export_plan(const TextureDocument &,const QJsonObject &);
QJsonObject automation_export_files(const AutomationExportPlan &);
}
