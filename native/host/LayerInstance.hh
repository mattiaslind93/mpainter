#pragma once
#include <QJsonObject>
#include <QUuid>
#include <optional>
#include <stdexcept>
#include <utility>
namespace bts {
struct LayerInstance {QUuid source_set,source_layer;};
inline QJsonValue layer_instance_json(const std::optional<LayerInstance> &instance){if(!instance)return QJsonValue();return QJsonObject{{"set",instance->source_set.toString(QUuid::WithoutBraces)},{"layer",instance->source_layer.toString(QUuid::WithoutBraces)}};}
inline std::optional<LayerInstance> read_layer_instance(const QJsonValue &value){if(value.isUndefined()||value.isNull())return {};if(!value.isObject())throw std::invalid_argument("Invalid layer instance reference");const auto object=value.toObject();LayerInstance result;for(auto [key,target]:{std::pair{"set",&result.source_set},std::pair{"layer",&result.source_layer}}){const auto text=object[key].toString();if(!object[key].isString()||text.size()>38||(*target=QUuid(text)).isNull())throw std::invalid_argument("Invalid layer instance identity");}return result;}
}
