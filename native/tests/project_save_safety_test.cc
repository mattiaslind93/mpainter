#include "ProjectFileSafety.hh"
#include "PackedFileLimit.hh"
#include "SanitizeReadback.hh"
#include "PaintPixels.hh"
#include "ProjectSaveConflict.hh"
#include <QApplication>
#include <QTemporaryDir>
#include <QTimer>
#include <cassert>
#include <limits>

template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception &){rejected=true;}assert(rejected);}
void write(const QString &path,const QByteArray &bytes){QFile file(path);assert(file.open(QIODevice::WriteOnly));assert(file.write(bytes)==bytes.size());}
QByteArray read(const QString &path){QFile file(path);assert(file.open(QIODevice::ReadOnly));return file.readAll();}
int main(int argc,char **argv){
  QApplication app(argc,argv);QTemporaryDir dir;assert(dir.isValid());
  const auto path=dir.filePath(QStringLiteral("m\u00e5larprojekt-\u753b\u5e03.blend"));
  rejects([&]{bts::ProjectFileIdentity::read(dir.path());});
  const auto missing=bts::ProjectFileIdentity::read(path);assert(!missing.exists);
  const auto first=bts::durable_project_write(path,missing,[](const QString &p){write(p,"first");});
  assert(first==bts::ProjectFileIdentity::read(path));
  const auto second=bts::durable_project_write(path,first,[](const QString &p){write(p,"second");});
  assert(read(path)=="second"&&read(path+"1")=="first");assert(first!=second);
  // Failure before publication preserves both the project and its backup.
  rejects([&]{bts::durable_project_write(path,second,[](const QString &p){write(p,"partial");throw std::runtime_error("full disk");});});
  assert(read(path)=="second"&&read(path+"1")=="first");
  write(path,"external edit");rejects([&]{bts::durable_project_write(path,second,[](const QString &){assert(false);});});
  auto token=bts::ProjectFileIdentity::read(path);
  // Replacement with the same length still conflicts (inode/mtime).
  {QSaveFile replacement(path);assert(replacement.open(QIODevice::WriteOnly));assert(replacement.write("external edit")==13);assert(replacement.commit());}
  rejects([&]{bts::require_project_identity(path,token);});
  token=bts::ProjectFileIdentity::read(path);
  rejects([&]{bts::durable_project_write(path,token,[&](const QString &p){write(p,"new");write(path,"raced");});});
  assert(read(path)=="raced");assert(QFile::remove(path));rejects([&]{bts::require_project_identity(path,token);});
  // A backup destination failure must not overwrite the existing project.
  const auto blocked=dir.filePath("blocked.blend");write(blocked,"original");
  assert(QDir().mkdir(blocked+"1"));
  rejects([&]{bts::durable_project_write(blocked,bts::ProjectFileIdentity::read(blocked),[](const QString &p){write(p,"new");});});
  assert(read(blocked)=="original"&&QFileInfo(blocked+"1").isDir());
  const std::uint64_t limit=std::uint64_t(1)<<31;
  bts::require_packed_file_size(limit-1,"test");rejects([&]{bts::require_packed_file_size(limit,"Stroke archive");});rejects([&]{bts::require_packed_file_size(limit+1,"Packed image");});
  std::array<bts::PaintPixels::Pixel,64*64> page{};
  page[0]={std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-70000,1};
  page[1]={1,2,3,0};page[2]={1,2,3,-1};page[3]={0,0,0,4};
  std::size_t fixes=0;for(auto &pixel:page)fixes+=bts::sanitize_readback_pixel(pixel.data());assert(fixes==11);
  assert(page[0][0]==0&&page[0][1]==65504&&page[0][2]==-65504);assert(page[1]==bts::PaintPixels::Pixel{});assert(page[2]==bts::PaintPixels::Pixel{});assert(page[3][3]==1);
  bts::PaintPixels pixels;pixels.assign_page(128,0,0,page);assert(pixels.page_count(0)==1);
  for(const auto &[text,choice]:{std::pair{"Cancel",bts::ProjectSaveChoice::Cancel},std::pair{"Overwrite",bts::ProjectSaveChoice::Overwrite},std::pair{"Save As",bts::ProjectSaveChoice::SaveAs}}){
    QTimer::singleShot(0,[&]{auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());assert(box);assert(box->defaultButton()->text().contains("Cancel"));for(auto *button:box->buttons())if(button->text().contains(text)){button->click();return;}assert(false);});
    assert(bts::ask_project_save_conflict(nullptr,path)==choice);
  }
}
