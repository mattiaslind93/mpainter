// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QString>
#include <cstdint>
#include <limits>
#include <vector>

namespace bts::sbsar_metadata {
using TokenId = std::size_t;
inline constexpr TokenId no_token = std::numeric_limits<TokenId>::max();
struct Attribute { QString name, namespace_uri, value; };
// Flat, ordered XML events: no recursive ownership or traversal. Original bytes
// additionally retain lexical details (quotes, namespace placement, CDATA, etc.).
enum class Kind { Start, End, Text, Comment, ProcessingInstruction, Doctype };
struct Token {
  Kind kind{};
  TokenId parent = no_token, matching = no_token;
  QString name, namespace_uri, text;
  std::vector<Attribute> attributes, namespaces;
  qint64 line = 0, column = 0;
  bool cdata = false;
};
struct Reference { TokenId input = no_token, output = no_token; QString raw_uid; };
struct Graph {
  TokenId token = no_token;
  std::vector<TokenId> inputs, outputs, presets;
  std::vector<Reference> output_references;
};
struct Description {
  QByteArray original_xml;
  std::vector<Token> tokens;
  TokenId root = no_token;
  QString format_version, doctype_name, doctype_public_id, doctype_system_id;
  bool known_schema = false;
  std::vector<Graph> graphs;
  std::vector<QString> diagnostics;
  std::uint64_t accounted_bytes = 0;
};
struct ReadOptions {
};
Description read_xml(QByteArray bytes, ReadOptions options = {});
Description read_xml_file(const QString &path, ReadOptions options = {});
QString attribute(const Token &, QStringView name);
QByteArray dump_json(const Description &);
// Exposed checked decimal primitive also used for counts and UID references.
std::uint64_t decimal(QStringView text);
}
