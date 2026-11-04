#include "ResourceBudget.hh"
#include "Recovery.hh"
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QVBoxLayout>
#include <stdexcept>
namespace bts {
RecoveryStore::RecoveryStore(QString root):root_(QDir::cleanPath(root)){if(!QDir::isAbsolutePath(root_))throw std::invalid_argument("Recovery directory must be absolute");}
QString RecoveryStore::next_path(const QUuid &project) const {
  if(project.isNull()||!QDir().mkpath(root_))throw std::runtime_error("Cannot create recovery directory");const QDir root(root_);const auto prefix=project.toString(QUuid::WithoutBraces);const QString first=root.filePath(prefix+"-0.blend"),second=root.filePath(prefix+"-1.blend");if(!QFileInfo::exists(first))return first;if(!QFileInfo::exists(second))return second;return QFileInfo(first).lastModified()<=QFileInfo(second).lastModified()?first:second;
}
void RecoveryStore::record(const QUuid &project,const QString &snapshot,const QString &source) const {
  const auto file=QFileInfo(snapshot);const auto root=QFileInfo(root_).canonicalFilePath();if(!file.isFile()||file.canonicalPath()!=root||file.isSymLink())throw std::invalid_argument("Recovery snapshot is outside its store");
  if(source.contains(QChar::Null))throw std::invalid_argument("Recovery source contains a path terminator");
  const auto project_text=project.toString(QUuid::WithoutBraces),time=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),name=file.fileName();
  const auto characters=resource_sum(source.size(),resource_sum(name.size(),resource_sum(project_text.size(),time.size())));
  // JSON may escape one UTF-16 code unit as six ASCII bytes, alongside its Qt copy.
  require_texture_ram(resource_sum(resource_product(characters,sizeof(QChar)+6),5*sizeof(QJsonValue)),"Recovery manifest JSON serialization");
  const auto bytes=QJsonDocument(QJsonObject{{"schema",1},{"project",project_text},{"source",source},{"snapshot",name},{"time",time}}).toJson(QJsonDocument::Compact);QSaveFile manifest(snapshot+".json");manifest.setDirectWriteFallback(false);if(!manifest.open(QIODevice::WriteOnly)||manifest.write(bytes)!=bytes.size()||!manifest.commit())throw std::runtime_error("Cannot commit recovery manifest");
}
std::vector<RecoveryEntry> RecoveryStore::entries() const {
  std::vector<RecoveryEntry> result;const QDir root(root_);if(!root.exists())return result;const auto canonical=QFileInfo(root_).canonicalFilePath();const QRegularExpression pattern("^([0-9a-fA-F-]{36})-[01]\\.blend$");
  for(const auto &file:root.entryInfoList({"*.blend"},QDir::Files,QDir::Time)){const auto match=pattern.match(file.fileName());if(!match.hasMatch()||file.isSymLink()||file.canonicalPath()!=canonical||file.size()<32)continue;const auto id=QUuid(match.captured(1));if(id.isNull())continue;RecoveryEntry entry{file.absoluteFilePath(),{},file.lastModified().toUTC().toString(Qt::ISODateWithMs),id};QFile manifest(file.absoluteFilePath()+".json");
    try {
      const QFileInfo info(manifest);const auto size=info.size();
      if(info.isFile()&&!info.isSymLink()&&info.canonicalPath()==canonical&&size>=0&&std::uint64_t(size)<=std::uint64_t(QByteArray::maxSize())&&manifest.open(QIODevice::ReadOnly)){
        require_texture_ram(resource_product(std::uint64_t(size),3),"Recovery manifest JSON decoding");
        const auto bytes=manifest.read(size);QJsonParseError error;
        if(bytes.size()==size&&manifest.atEnd()&&!bytes.contains('\0')){
          const auto document=QJsonDocument::fromJson(bytes,&error);
          if(error.error==QJsonParseError::NoError&&document.isObject()){
            const auto object=document.object();const auto source=object["source"].toString();
            if(object["schema"].toInt()==1&&object["project"].toString()==id.toString(QUuid::WithoutBraces)&&object["snapshot"].toString()==file.fileName()&&!source.contains(QChar::Null))entry.source=source;
          }
        }
      }
    }catch(const std::exception &){/* A damaged or unaffordable manifest must not hide a complete snapshot. */}
    result.push_back(std::move(entry));}
  return result;
}
QString choose_recovery(QWidget *parent,const RecoveryStore &store){
  QDialog dialog(parent);dialog.setObjectName("recoveryDialog");dialog.setWindowTitle("Recover project snapshot");dialog.resize(800,360);auto *layout=new QVBoxLayout(&dialog);layout->addWidget(new QLabel("Open a complete snapshot as an unsaved recovered project. Save As preserves the original project.",&dialog));auto *list=new QListWidget(&dialog);list->setObjectName("recoverySnapshots");layout->addWidget(list,1);for(const auto &entry:store.entries()){const auto name=entry.source.isEmpty()?QString("Untitled"):QFileInfo(entry.source).fileName();auto *item=new QListWidgetItem(name+" — "+entry.time+"\n"+(entry.source.isEmpty()?entry.path:entry.source),list);item->setData(Qt::UserRole,entry.path);}if(list->count())list->setCurrentRow(0);else {list->addItem("No recovery snapshots");list->setCurrentRow(0);}auto *buttons=new QDialogButtonBox(QDialogButtonBox::Open|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Open)->setEnabled(!list->currentItem()->data(Qt::UserRole).toString().isEmpty());QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);layout->addWidget(buttons);return dialog.exec()==QDialog::Accepted?list->currentItem()->data(Qt::UserRole).toString():QString();
}
}
