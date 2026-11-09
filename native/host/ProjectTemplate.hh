#pragma once
#include "TextureDocument.hh"
#include <QString>
#include <string_view>
namespace bts {
QJsonObject make_project_template(const TextureDocument &,
                                  std::uint32_t default_set,
                                  const QJsonObject &display,
                                  const QJsonObject &environment,
                                  const QJsonObject &resource,
                                  const QJsonObject &baking);
void validate_project_template(const QJsonObject &);
QJsonObject read_project_template(const QString &);
void write_project_template(const QString &, const QJsonObject &);
QString project_template_directory();
NewProjectOptions project_template_options(const QJsonObject &);
void apply_project_template(TextureDocument &, const QJsonObject &);
bool builtin_project_template(std::string_view);
void apply_builtin_project_template(TextureDocument &, std::string_view);
}  // namespace bts
