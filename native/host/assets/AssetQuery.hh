// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AssetCatalog.hh"
namespace bts {
// The reference-grounded subset: AND-combined name/scope/usage/path terms,
// case-insensitive substring matching and a trailing '=' for exact matching.
class AssetQuery {
 public:
  explicit AssetQuery(const QString &query)
  {
    QString token;
    bool quoted = false;
    const auto finish = [&] {
      if (token.isEmpty())
        return;
      Clause clause;
      const auto colon = token.indexOf(':');
      if (colon >= 0) {
        clause.field = token.left(colon).toLower();
        token = token.mid(colon + 1);
        if (!QStringList{"n", "s", "u", "p"}.contains(clause.field))
          valid_ = false;
      }
      if (token.endsWith('=')) {
        clause.exact = true;
        token.chop(1);
      }
      clause.pattern = token;
      if (token.isEmpty())
        valid_ = false;
      clauses_.push_back(clause);
      token.clear();
    };
    for (qsizetype i = 0; i < query.size(); ++i) {
      const auto c = query[i];
      if (c == '\\' && i + 1 < query.size() && (query[i + 1] == '"' || query[i + 1] == '\\')) {
        token += query[++i];
        continue;
      }
      if (c == '"') {
        quoted = !quoted;
        continue;
      }
      if (c.isSpace() && !quoted)
        finish();
      else
        token += c;
    }
    finish();
    if (quoted)
      valid_ = false;
  }
  bool matches(const StudioAsset &asset) const
  {
    if (!valid_)
      return false;
    for (const auto &clause : clauses_) {
      const auto match = [&](const QString &value) {
        return clause.exact ? QString::compare(value, clause.pattern, Qt::CaseInsensitive) == 0 :
                              value.contains(clause.pattern, Qt::CaseInsensitive);
      };
      bool matched = false;
      if (clause.field == "s")
        matched = match(asset_scope_name(asset.scope));
      else if (clause.field == "u") {
        for (const auto &usage : asset.usages)
          matched |= match(usage);
      }
      else if (clause.field == "p")
        matched = match(asset.path);
      else {
        matched = match(asset.name);
        if (clause.field.isEmpty())
          for (const auto &term : asset.search_terms)
            matched |= match(term);
      }
      if (!matched)
        return false;
    }
    return true;
  }

 private:
  struct Clause {
    QString field, pattern;
    bool exact = false;
  };
  std::vector<Clause> clauses_;
  bool valid_ = true;
};
}  // namespace bts
