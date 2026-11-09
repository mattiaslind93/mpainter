#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <atomic>
namespace bts {
// USD material library, not mesh geometry. The sibling MaterialX document carries
// the pinned node definition; renderers need MaterialX support. No pixel work.
void package_openpbr_usd_files(const QString &layer,
                               const QString &package,
                               const QStringList &relative_files,
                               const std::atomic_bool &cancel);
QByteArray openpbr_usd_document(const QByteArray &materialx, const QString &materialx_filename);
}  // namespace bts
