#pragma once
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QString>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace bts {
struct ProjectFileIdentity {
  bool exists=false;
  std::uint64_t device=0,inode=0,size=0;
  std::int64_t seconds=0,nanoseconds=0;
  bool operator==(const ProjectFileIdentity &) const = default;
  static ProjectFileIdentity read(const QString &path) {
    struct stat info{};
    if (::stat(QFile::encodeName(path).constData(), &info)) {
      if(errno==ENOENT)return {};
      throw std::runtime_error("Cannot inspect project on disk: "+std::string(std::strerror(errno)));
    }
    if(!S_ISREG(info.st_mode))throw std::runtime_error("Project path is not a regular file");
#ifdef __APPLE__
    const auto modified=info.st_mtimespec;
#else
    const auto modified=info.st_mtim;
#endif
    return {true,std::uint64_t(info.st_dev),std::uint64_t(info.st_ino),std::uint64_t(info.st_size),modified.tv_sec,modified.tv_nsec};
  }
};
inline void require_project_identity(const QString &path,const ProjectFileIdentity &expected) {
  if(ProjectFileIdentity::read(path)!=expected)
    throw std::runtime_error("Project changed on disk during Open/Save. Save again to choose Overwrite, Save As or Cancel.");
}
inline void sync_project_fd(int fd) {
  int result;do{result=::fsync(fd);}while(result<0&&errno==EINTR);
  if(result<0)throw std::runtime_error("Cannot flush project to disk: "+std::string(std::strerror(errno)));
}
inline void sync_project_directory(const QString &path) {
  const int fd=::open(QFile::encodeName(QFileInfo(path).absolutePath()).constData(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
  if(fd<0)throw std::runtime_error("Cannot open project directory for durable save");
  try{sync_project_fd(fd);}catch(...){::close(fd);throw;}::close(fd);
}
// The old target survives every encode, flush or backup failure. Only a complete,
// synced file replaces it; the backup is committed before the final rename.
template<class Writer>
ProjectFileIdentity durable_project_write(const QString &path,
    std::optional<ProjectFileIdentity> expected,Writer &&writer) {
  const auto original=expected.value_or(ProjectFileIdentity::read(path));
  require_project_identity(path,original);
  QTemporaryFile staged(path+".saving-XXXXXX");
  if(!staged.open())throw std::runtime_error("Cannot create temporary project beside target");
  const auto temporary=staged.fileName();staged.close();
  writer(temporary);
  QFile complete(temporary);
  if(!complete.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot reopen completed project for flush");
  sync_project_fd(complete.handle());
  const auto saved=ProjectFileIdentity::read(temporary);
  require_project_identity(path,original);
  if(original.exists){
    QFile source(path);QSaveFile backup(path+"1");backup.setDirectWriteFallback(false);
    if(!source.open(QIODevice::ReadOnly)||!backup.open(QIODevice::WriteOnly))
      throw std::runtime_error("Cannot create .blend1 backup; project was not overwritten");
    while(!source.atEnd()){
      const auto bytes=source.read(1024*1024);
      if(bytes.isEmpty()||backup.write(bytes)!=bytes.size())throw std::runtime_error("Cannot write .blend1 backup");
    }
    if(!backup.flush())throw std::runtime_error("Cannot flush .blend1 backup");
    sync_project_fd(backup.handle());
    require_project_identity(path,original);
    if(!backup.commit())throw std::runtime_error("Cannot commit .blend1 backup");
    sync_project_directory(path);
  }
  require_project_identity(path,original);
  if(::rename(QFile::encodeName(temporary).constData(),QFile::encodeName(path).constData()))
    throw std::runtime_error("Cannot replace project; previous file and backup retained");
  sync_project_directory(path);
  return saved;
}
}
