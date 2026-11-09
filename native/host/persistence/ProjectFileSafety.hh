#pragma once
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryFile>
#include <QString>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <system_error>
#ifdef Q_OS_WIN
#  include <QtCore/qt_windows.h>
#  include <io.h>
#else
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace bts {
#ifdef Q_OS_WIN
inline QString project_native_path(const QString &path)
{
  auto native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
  if (native.startsWith(QStringLiteral("\\\\?\\")))
    return native;
  if (native.startsWith(QStringLiteral("\\\\")))
    return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
  return QStringLiteral("\\\\?\\") + native;
}
#endif
struct ProjectFileIdentity {
  bool exists = false;
  std::uint64_t device = 0, inode = 0, size = 0;
  std::int64_t seconds = 0, nanoseconds = 0;
  bool operator==(const ProjectFileIdentity &) const = default;
  static ProjectFileIdentity read(const QString &path)
  {
#ifdef Q_OS_WIN
    const auto native = project_native_path(path);
    const auto file = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),
                                  FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS,
                                  nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      const auto error = GetLastError();
      if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return {};
      throw std::system_error(
          int(error), std::system_category(), "Cannot inspect project on disk");
    }
    BY_HANDLE_FILE_INFORMATION info{};
    const bool valid = GetFileInformationByHandle(file, &info);
    const auto error = GetLastError();
    const auto type = GetFileType(file);
    CloseHandle(file);
    if (!valid)
      throw std::system_error(
          int(error), std::system_category(), "Cannot inspect project on disk");
    if (type != FILE_TYPE_DISK || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
      throw std::runtime_error("Project path is not a regular file");
    const auto ticks = (std::uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) |
                       info.ftLastWriteTime.dwLowDateTime;
    return {true,
            info.dwVolumeSerialNumber,
            (std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow,
            (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow,
            std::int64_t(ticks / 10000000) - 11644473600LL,
            std::int64_t(ticks % 10000000) * 100};
#else
    struct stat info{};
    if (::stat(QFile::encodeName(path).constData(), &info)) {
      if (errno == ENOENT)
        return {};
      throw std::runtime_error("Cannot inspect project on disk: " +
                               std::string(std::strerror(errno)));
    }
    if (!S_ISREG(info.st_mode))
      throw std::runtime_error("Project path is not a regular file");
#  ifdef __APPLE__
    const auto modified = info.st_mtimespec;
#  else
    const auto modified = info.st_mtim;
#  endif
    return {true,
            std::uint64_t(info.st_dev),
            std::uint64_t(info.st_ino),
            std::uint64_t(info.st_size),
            modified.tv_sec,
            modified.tv_nsec};
#endif
  }
};
inline void require_project_identity(const QString &path, const ProjectFileIdentity &expected)
{
  if (ProjectFileIdentity::read(path) != expected)
    throw std::runtime_error(
        "Project changed on disk during Open/Save. Save again to choose Overwrite, Save As or Cancel.");
}
inline void sync_project_fd(int fd)
{
  if (fd < 0)
    throw std::runtime_error("Cannot obtain project file descriptor for flush");
#ifdef Q_OS_WIN
  const int result = ::_commit(fd);
#else
  int result;
  do {
    result = ::fsync(fd);
  } while (result < 0 && errno == EINTR);
#endif
  if (result < 0)
    throw std::runtime_error("Cannot flush project to disk: " + std::string(std::strerror(errno)));
}
inline void sync_project_directory(const QString &path)
{
#ifdef Q_OS_WIN
  // Windows has no portable directory-fsync equivalent. File contents are
  // committed before replacement; MoveFileExW requests write-through below.
  (void)path;
#else
  const int fd = ::open(QFile::encodeName(QFileInfo(path).absolutePath()).constData(),
                        O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0)
    throw std::runtime_error("Cannot open project directory for durable save");
  try {
    sync_project_fd(fd);
  }
  catch (...) {
    ::close(fd);
    throw;
  }
  ::close(fd);
#endif
}
inline void replace_project_file(const QString &temporary, const QString &path)
{
#ifdef Q_OS_WIN
  const auto source = project_native_path(temporary), target = project_native_path(path);
  if (!MoveFileExW(reinterpret_cast<LPCWSTR>(source.utf16()),
                   reinterpret_cast<LPCWSTR>(target.utf16()),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::system_error(int(GetLastError()),
                            std::system_category(),
                            "Cannot replace project; previous file and backup retained");
#else
  if (::rename(QFile::encodeName(temporary).constData(), QFile::encodeName(path).constData()))
    throw std::runtime_error("Cannot replace project; previous file and backup retained");
#endif
}
// The old target survives every encode, flush or backup failure. Only a complete,
// synced file replaces it; the backup is committed before the final rename.
template<class Writer>
ProjectFileIdentity durable_project_write(const QString &path,
                                          std::optional<ProjectFileIdentity> expected,
                                          Writer &&writer)
{
  const auto original = expected.value_or(ProjectFileIdentity::read(path));
  require_project_identity(path, original);
  const auto temporary = [&] {
    QTemporaryFile staged(path + ".saving-XXXXXX");
    if (!staged.open())
      throw std::runtime_error("Cannot create temporary project beside target");
    const auto name = staged.fileName();
    staged.setAutoRemove(false);
    return name;
  }();  // Destroy the object: QTemporaryFile::close() retains its native handle.
  const auto cleanup = qScopeGuard([&] { QFile::remove(temporary); });
  writer(temporary);
  QFile complete(temporary);
#ifdef Q_OS_WIN
  const auto flush_mode = QIODevice::ReadWrite;  // FlushFileBuffers requires write access.
#else
  const auto flush_mode = QIODevice::ReadOnly;
#endif
  if (!complete.open(flush_mode))
    throw std::runtime_error("Cannot reopen completed project for flush");
  sync_project_fd(complete.handle());
  complete.close();  // Windows cannot replace an open file without delete sharing.
  const auto saved = ProjectFileIdentity::read(temporary);
  require_project_identity(path, original);
  if (original.exists) {
    QFile source(path);
    QSaveFile backup(path + "1");
    backup.setDirectWriteFallback(false);
    if (!source.open(QIODevice::ReadOnly) || !backup.open(QIODevice::WriteOnly))
      throw std::runtime_error("Cannot create .blend1 backup; project was not overwritten");
    while (!source.atEnd()) {
      const auto bytes = source.read(1024 * 1024);
      if (bytes.isEmpty() || backup.write(bytes) != bytes.size())
        throw std::runtime_error("Cannot write .blend1 backup");
    }
    if (!backup.flush())
      throw std::runtime_error("Cannot flush .blend1 backup");
    sync_project_fd(backup.handle());
    require_project_identity(path, original);
    if (!backup.commit())
      throw std::runtime_error("Cannot commit .blend1 backup");
    sync_project_directory(path);
  }
  require_project_identity(path, original);
  replace_project_file(temporary, path);
  sync_project_directory(path);
  return saved;
}
}  // namespace bts
