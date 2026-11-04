#pragma once
#include "ResourceBudget.hh"
#include <QString>
#include <stdexcept>
namespace bts {
inline QString smart_preset_name(const QString &text){
 require_texture_ram(resource_product(text.size(),16),"Smart preset display name");
 const auto name=text.trimmed();
 if(name.isEmpty()||name.contains(QChar::Null))throw std::invalid_argument("Choose a nonempty smart-preset name without NUL characters");
 return name;
}
}
