#pragma once
#include "AssetCatalog.hh"
class QWidget;
namespace bts {std::optional<StudioAsset> choose_resource_import(QWidget *,bool project_available,const QString &initial_path={});}
