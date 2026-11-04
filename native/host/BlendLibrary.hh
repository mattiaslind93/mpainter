#include "ResourceBudget.hh"
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <set>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <vector>

namespace bts {

struct BlendMeshSelection {
  std::vector<std::string> objects, collections;
  bool operator==(const BlendMeshSelection &) const = default;

  bool empty() const { return objects.empty() && collections.empty(); }
  void validate(bool require_selection = false) const
  {
    if ((require_selection && empty())) {
      throw std::invalid_argument("Select at least one object or collection");
    }
    for (const auto *names : {&objects, &collections}) {
      std::set<std::string> unique;
      for (const auto &name : *names) {
        // Native ID.name[258] reserves two type bytes and one NUL byte.
        if(name.empty()||name.size()>255||name.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid Blender library ID name");
        const auto text = QString::fromUtf8(name.data(), qsizetype(name.size()));
        if (text.toUtf8().toStdString() != name || !unique.insert(name).second)
        {
          throw std::invalid_argument("Invalid or duplicate Blender library selection");
        }
      }
    }
  }
  QJsonObject to_json() const
  {
    validate();
    QJsonArray object_names, collection_names;
    for (const auto &name : objects) object_names.append(QString::fromStdString(name));
    for (const auto &name : collections) collection_names.append(QString::fromStdString(name));
    return {{"objects", object_names}, {"collections", collection_names}};
  }
  static BlendMeshSelection from_json(const QJsonValue &value)
  {
    if (!value.isObject()) throw std::invalid_argument("Missing Blender library selection");
    const auto data = value.toObject();
    if (data.size() != 2 || !data["objects"].isArray() || !data["collections"].isArray()) {
      throw std::invalid_argument("Invalid Blender library selection metadata");
    }
    BlendMeshSelection result;
    const auto read = [](const QJsonArray &entries, std::vector<std::string> &names) {
      require_texture_ram(resource_product(entries.size(),sizeof(std::string)),"Blender library selection");
      for (const auto &entry : entries) {
        if (!entry.isString()) throw std::invalid_argument("Invalid Blender library entry name");
        names.push_back(entry.toString().toStdString());
      }
    };
    read(data["objects"].toArray(), result.objects);
    read(data["collections"].toArray(), result.collections);
    result.validate();
    return result;
  }
};

struct BlendLibraryCatalog {
  std::string source;
  std::vector<std::string> objects, collections;
};

// Metadata-only native BlendHandle IO, as used by Blender's file-browser workers.
// Does not load a Main, instantiate IDs, run scripts or change the live scene.
BlendLibraryCatalog inspect_blend_library(const std::string &, std::stop_token = {});
bool is_blend_library(const std::string &);

}  // namespace bts
