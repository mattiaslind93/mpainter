#pragma once
#include <QIcon>
#include <QImage>
#include <QString>
#include <functional>
#include "IMB_thumbs.hh"
class QFileDialog;
namespace bts {
enum class BrowserFileType {Folder,Blank,Blend,Backup,Image,Movie,Script,Sound,Font,Object,Text,Archive,Volume,SmartMaterial,MaterialArchive,Drive,Parent,Redirect,FolderLarge,ParentLarge,FileLarge,Count};
BrowserFileType browser_file_type(const QString &path);
// Pre-rasterized resource atlas, cached once on the GUI thread.
QIcon browser_file_icon(BrowserFileType type);
QIcon browser_file_large_icon(BrowserFileType type);
void setup_browser_controls(QFileDialog *dialog,int mode,int zoom);
void resume_browser_previews(QFileDialog *dialog);
void cancel_browser_previews(QFileDialog *dialog);
// Terminal host/application shutdown only; never called on dialog close.
void shutdown_browser_previews();
using BrowserThumbnailLoader=std::function<QImage(const QString &,bool,const blender::ThumbCancellationToken &)>;
// Installed by the native host adapter; tests can supply a controlled decoder.
void set_browser_thumbnail_loader(BrowserThumbnailLoader loader);
BrowserThumbnailLoader browser_thumbnail_loader();
}
