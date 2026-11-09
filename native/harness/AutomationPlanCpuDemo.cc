// SPDX-License-Identifier: GPL-3.0-or-later
#include "AutomationExportPlan.hh"
#include <QJsonArray>
namespace bts {
QJsonObject verify_automation_export_plan(const TextureDocument &source)
{
  const auto require = [](bool condition, const char *message) {
    if (!condition)
      throw std::runtime_error(message);
  };
  const auto before = source.to_json();
  const auto plan = automation_export_plan(
      source, {{"directory", "/missing/planning-only"}, {"preset", "document"}});
  const auto manifest = automation_export_files(plan);
  require(!manifest["files"].toArray().isEmpty(), "Automation preview returned no textures");
  require(manifest["by_texture_set"].toObject().size() == int(plan.selected.size()),
          "Preview did not group by Texture Set");
  for (int format = 0; format < 5; ++format) {
    const auto value = QStringList{"png8", "png16", "exr16", "exr32", "jpeg8"}[format];
    const auto mapped = automation_export_plan(source,
                                               {{"directory", "/missing/planning-only"},
                                                {"preset", "document"},
                                                {"format", value},
                                                {"size", 256}});
    for (const auto &file : export_file_list(mapped.request)) {
      require(file.resolution == 256 && file.height == 256, "Automation size override missing");
      require(QString::fromStdString(file.name).endsWith(format < 2 ? ".png" :
                                                         format < 4 ? ".exr" :
                                                                      ".jpg"),
              "Automation global encoding missing");
    }
  }
  for (int size : {8193, 16384, 2147483647}) {
    bool rejected = false;
    try {
      automation_export_plan(source, {{"directory", "/missing/planning-only"}, {"size", size}});
    }
    catch (const std::invalid_argument &error) {
      rejected = std::string(error.what()).find("8192 (8K)") != std::string::npos;
    }
    require(rejected, "Automation export exceeded the 8K cap without an explanation");
  }
  const auto maximum = automation_export_plan(
      source, {{"directory", "/missing/planning-only"}, {"preset", "document"}, {"size", 8192}});
  for (const auto &file : export_file_list(maximum.request))
    require(file.resolution == 8192 && file.height == 8192,
            "8K automation export was not preserved");
  for (const auto &bad :
       std::vector<QJsonObject>{{{"directory", "relative"}},
                                {{"preset", "missing"}},
                                {{"format", "jpeg32"}},
                                {{"size", 513}},
                                {{"padding", 99}},
                                {{"texture_sets", QJsonArray{QUuid::createUuid().toString()}}},
                                {{"texture_sets", QJsonArray{}}},
                                {{"bogus", true}}})
  {
    auto options = bad;
    options.insert("directory",
                   bad.contains("directory") ? bad["directory"] :
                                               QJsonValue("/missing/planning-only"));
    bool rejected = false;
    try {
      automation_export_plan(source, options);
    }
    catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "Malformed automation export option accepted");
  }
  auto document = source;
  std::vector<QUuid> ids;
  for (const auto &set : document.texture_sets())
    ids.push_back(set.id);
  auto config = read_export_configuration({}, ids);
  config.directory = "/missing/planning-only";
  ExportOutputPreset preset;
  preset.name = "Automation packed precision";
  preset.standard_outputs = false;
  preset.packed = PackedOutput{};
  preset.packed->format = ExportFormat::Exr32;
  preset.outputs = {
      {"$textureSet_glossiness", ExportMapSource::Glossiness, false, ExportFormat::Png16}};
  config.presets = {preset};
  config.preset = preset.id;
  document.set_export_configuration(export_configuration_json(config));
  const auto saved = automation_export_plan(document, {});
  require(saved.request.packed->format == ExportFormat::Exr32 &&
              saved.request.outputs[0].format == ExportFormat::Png16,
          "Automation lost saved per-output formats");
  const auto usd = automation_export_plan(
      source, {{"directory", "/missing/planning-only"}, {"preset", "usd-metal-rough"}});
  require(usd.usd, "USD automation preset not detected");
  for (int c : {0, 1, 2, 7})
    require(usd.request.formats[c] == ExportFormat::Jpeg8, "USD recipe encoding changed");
  require(automation_export_files(usd)["files"].toArray().size() ==
              int(export_file_list(usd.request).size()) + 2,
          "USD preview lost USDC/USDZ");
  require(source.to_json() == before, "Filename planning modified the document");
  return {{"pure_preview", true},
          {"all_encodings", 5},
          {"malformed_options", 8},
          {"saved_v6_precision", true},
          {"usd_package_preview", true},
          {"source_unchanged", true}};
}
}  // namespace bts
