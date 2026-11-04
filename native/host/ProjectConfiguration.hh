#pragma once
#include <QString>
#include "ProjectSettings.hh"
#include <functional>
class QWidget;
namespace bts {class TextureDocument;bool configure_project_mesh(QWidget *,const TextureDocument &,const std::function<void(const QString &,bool,NormalMapFormat,bool,bool,const AutoUnwrapOptions &,const UsdImportOptions &,const UnitSettings &,const BlendMeshSelection &)> &replace);}
