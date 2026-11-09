#include "ResourceAvailability.hh"
#include "BlendLibrary.hh"

#include <QFileInfo>
#include <QDateTime>
#include <algorithm>
#include <memory>

#include "BLI_linklist.h"
#include "BLO_readfile.hh"
#include "DNA_ID.h"

namespace bts {
using namespace blender;

bool is_blend_library(const std::string &path)
{
  return QFileInfo(QString::fromStdString(path)).suffix().compare("blend", Qt::CaseInsensitive) ==
         0;
}

BlendLibraryCatalog inspect_blend_library(const std::string &path, std::stop_token cancellation)
{
  const auto check = [&] {
    if (cancellation.stop_requested())
      throw std::runtime_error("Blender library listing cancelled");
  };
  check();
  const QFileInfo file(QString::fromStdString(path));
  if (!file.isAbsolute() || !file.isFile() || !file.isReadable() || file.size() <= 0 ||
      !is_blend_library(path))
  {
    throw std::invalid_argument("Choose a readable absolute .blend library");
  }
  BlendFileReadReport reports{};
  std::unique_ptr<BlendHandle, decltype(&BLO_blendhandle_close)> handle(
      BLO_blendhandle_from_file(path.c_str(), &reports), BLO_blendhandle_close);
  if (!handle)
    throw std::runtime_error("Cannot inspect Blender library: " + path);
  BlendLibraryCatalog result;
  result.source = path;
  const auto read = [&](short type, std::vector<std::string> &names) {
    check();
    int count = 0;
    std::unique_ptr<LinkNode, decltype(&BLI_linklist_freeN)> entries(
        BLO_blendhandle_get_datablock_names(handle.get(), type, false, &count),
        BLI_linklist_freeN);
    if (count < 0)
      throw std::runtime_error("Invalid Blender library entry count");

    for (auto *entry = entries.get(); entry; entry = entry->next) {
      check();
      names.emplace_back(static_cast<const char *>(entry->link));
    }
    std::sort(names.begin(), names.end());
  };
  read(ID_OB, result.objects);
  read(ID_GR, result.collections);
  check();
  const QFileInfo after(QString::fromStdString(path));
  if (after.size() != file.size() || after.lastModified() != file.lastModified()) {
    throw std::runtime_error("Blender library changed during listing");
  }
  return result;
}

}  // namespace bts
