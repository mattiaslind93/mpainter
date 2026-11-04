#pragma once
#include "TextureDocument.hh"
#include <optional>
class QWidget;
namespace bts {std::optional<std::vector<TextureSet>> choose_texture_set_assignment(QWidget *,const TextureDocument &,const SceneSnapshot &);}
